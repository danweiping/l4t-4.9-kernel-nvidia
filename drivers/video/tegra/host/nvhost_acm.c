/*
 * drivers/video/tegra/host/nvhost_acm.c
 *
 * Tegra Graphics Host Automatic Clock Management
 *
 * Copyright (c) 2010-2014, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/clk/tegra.h>
#include <linux/tegra-powergate.h>
#include <linux/tegra-soc.h>
#include <trace/events/nvhost.h>
#include <linux/platform_data/tegra_edp.h>
#include <linux/tegra_pm_domains.h>
#include <linux/nvhost_ioctl.h>

#include <tegra/mc.h>

#include "nvhost_acm.h"
#include "nvhost_scale.h"
#include "nvhost_channel.h"
#include "dev.h"
#include "bus_client.h"

#define ACM_SUSPEND_WAIT_FOR_IDLE_TIMEOUT	(2 * HZ)
#define POWERGATE_DELAY 			10
#define MAX_DEVID_LENGTH			16

static void nvhost_module_load_regs(struct platform_device *pdev, bool prod);
static int nvhost_module_toggle_slcg(struct notifier_block *nb,
				     unsigned long action, void *data);

#ifdef CONFIG_PM_GENERIC_DOMAINS
#if 0
static int nvhost_module_suspend(struct device *dev);
#endif
static int nvhost_module_power_on(struct generic_pm_domain *domain);
static int nvhost_module_power_off(struct generic_pm_domain *domain);
static int nvhost_module_prepare_poweroff(struct device *dev);
static int nvhost_module_finalize_poweron(struct device *dev);
#endif

static DEFINE_MUTEX(client_list_lock);

struct nvhost_module_client {
	struct list_head node;
	unsigned long constraint[NVHOST_MODULE_MAX_CLOCKS];
	unsigned long type[NVHOST_MODULE_MAX_CLOCKS];
	void *priv;
};

#ifdef CONFIG_ARCH_TEGRA
static void do_powergate_locked(int id)
{
	nvhost_dbg_fn("%d", id);
	if (id != -1 && tegra_powergate_is_powered(id))
		tegra_powergate_partition(id);
}

static void do_unpowergate_locked(int id)
{
	int ret = 0;
	if (id != -1) {
		ret = tegra_unpowergate_partition(id);
		if (ret)
			pr_err("%s: unpowergate failed: id = %d\n",
					__func__, id);
	}
}

static void do_module_reset_locked(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	if (pdata->reset) {
		pdata->reset(dev);
		return;
	}

	/* assert module and mc client reset */
	if (pdata->clocks[0].reset) {
		tegra_mc_flush(pdata->clocks[0].reset);
		tegra_periph_reset_assert(pdata->clk[0]);
	}

	if (pdata->clocks[1].reset) {
		tegra_mc_flush(pdata->clocks[1].reset);
		tegra_periph_reset_assert(pdata->clk[1]);
	}

	udelay(POWERGATE_DELAY);

	/* deassert reset */
	if (pdata->clocks[0].reset) {
		tegra_periph_reset_deassert(pdata->clk[0]);
		tegra_mc_flush_done(pdata->clocks[0].reset);
	}

	if (pdata->clocks[1].reset) {
		tegra_periph_reset_deassert(pdata->clk[1]);
		tegra_mc_flush_done(pdata->clocks[1].reset);
	}
}

static unsigned long nvhost_emc_bw_to_freq_req(unsigned long rate)
{
	return tegra_emc_bw_to_freq_req((unsigned long)(rate >> 10));
}
#else
static void do_powergate_locked(int id)
{
	nvhost_dbg_fn("%d", id);
}

static void do_unpowergate_locked(int id)
{
	nvhost_dbg_fn("");
}

static void do_module_reset_locked(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	nvhost_dbg_fn("");

	if (pdata->reset) {
		pdata->reset(dev);
		return;
	}
}
static unsigned long nvhost_emc_bw_to_freq_req(unsigned long rate)
{
	return 0;
}
#endif

void nvhost_module_reset(struct platform_device *dev, bool reboot)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	dev_dbg(&dev->dev,
		"%s: asserting %s module reset (id %d, id2 %d)\n",
		__func__, dev_name(&dev->dev),
		pdata->powergate_ids[0], pdata->powergate_ids[1]);

	mutex_lock(&pdata->lock);
	do_module_reset_locked(dev);
	mutex_unlock(&pdata->lock);

	if (reboot) {
		/* Load clockgating registers */
		nvhost_module_load_regs(dev, pdata->engine_can_cg);

		/* ..and execute engine specific operations (i.e. boot) */
		if (pdata->finalize_poweron)
			pdata->finalize_poweron(dev);
	}

	dev_dbg(&dev->dev, "%s: module %s out of reset\n",
		__func__, dev_name(&dev->dev));
}

void nvhost_module_busy_noresume(struct platform_device *dev)
{
	if (dev->dev.parent && (dev->dev.parent != &platform_bus))
		nvhost_module_busy_noresume(nvhost_get_parent(dev));

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_noresume(&dev->dev);
#endif
}

int nvhost_module_busy(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	int ret = 0;

	/* Explicitly turn on the host1x clocks
	 * - This is needed as host1x driver sets ignore_children = true
	 * to cater the use case of display clock ON but host1x clock OFF
	 * in OS-Idle-Display-ON case
	 * - This was easily done in ACM as it only checked the ref count
	 * of host1x (or any device for that matter) to be zero before
	 * turning off its clock
	 * - However, runtime PM checks to see if *ANY* child of device is
	 * in ACTIVE state and if yes, it doesn't suspend the parent. As a
	 * result of this, display && host1x clocks remains ON during
	 * OS-Idle-Display-ON case
	 * - The code below fixes this use-case
	 */
	if (dev->dev.parent && (dev->dev.parent != &platform_bus))
		ret = nvhost_module_busy(nvhost_get_parent(dev));

	if (ret)
		return ret;

#ifdef CONFIG_PM_RUNTIME
	ret = pm_runtime_get_sync(&dev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&dev->dev);
		if (dev->dev.parent && (dev->dev.parent != &platform_bus))
			nvhost_module_idle(nvhost_get_parent(dev));
		nvhost_err(&dev->dev, "failed to power on, err %d", ret);
		return ret;
	}
#else
	if (!pdata->booted && pdata->finalize_poweron) {
		ret = pdata->finalize_poweron(dev);
		if (ret < 0) {
			nvhost_err(&dev->dev, "failed to power on, err %d",
				   ret);
			return ret;
		}
		pdata->booted = true;
	}
#endif

	if (pdata->busy)
		pdata->busy(dev);

	return 0;
}

void nvhost_module_disable_poweroff(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	mutex_lock(&pdata->no_poweroff_req_mutex);
	pdata->no_poweroff_req_count++;
	if (!dev_pm_qos_request_active(&pdata->no_poweroff_req))
		dev_pm_qos_add_request(&dev->dev, &pdata->no_poweroff_req,
				DEV_PM_QOS_FLAGS, PM_QOS_FLAG_NO_POWER_OFF);
	mutex_unlock(&pdata->no_poweroff_req_mutex);
}

void nvhost_module_enable_poweroff(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	mutex_lock(&pdata->no_poweroff_req_mutex);
	pdata->no_poweroff_req_count--;
	if (!pdata->no_poweroff_req_count &&
	    dev_pm_qos_request_active(&pdata->no_poweroff_req))
		dev_pm_qos_remove_request(&pdata->no_poweroff_req);
	mutex_unlock(&pdata->no_poweroff_req_mutex);
}

void nvhost_module_idle_mult(struct platform_device *dev, int refs)
{
	int original_refs = refs;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

#ifdef CONFIG_PM_RUNTIME
	/* call idle callback only if the device is turned on. */
	if (atomic_read(&dev->dev.power.usage_count) == refs &&
	    pm_runtime_active(&dev->dev)) {
		if (pdata->idle)
			pdata->idle(dev);
	}

	while (refs--) {
		pm_runtime_mark_last_busy(&dev->dev);
		if (pdata->clockgate_delay)
			pm_runtime_put_sync_autosuspend(&dev->dev);
		else
			pm_runtime_put(&dev->dev);
	}
#else
	if (pdata->idle)
		pdata->idle(dev);
#endif

	/* Explicitly turn off the host1x clocks */
	if (dev->dev.parent && (dev->dev.parent != &platform_bus))
		nvhost_module_idle_mult(nvhost_get_parent(dev), original_refs);
}

int nvhost_module_get_rate(struct platform_device *dev, unsigned long *rate,
		int index)
{
	struct clk *c;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	int err = 0;

	c = pdata->clk[index];
	if (!c)
		return -EINVAL;

	/* Need to enable client to get correct rate */
	err = nvhost_module_busy(dev);
	if (err)
		return err;

	*rate = clk_get_rate(c);
	nvhost_module_idle(dev);
	return 0;
}

static int nvhost_module_update_rate(struct platform_device *dev, int index)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	unsigned long bw_constraint = 0, floor_rate = 0, pixelrate = 0;
	unsigned long rate = 0;
	struct nvhost_module_client *m;
	int ret;

	if (!pdata->clk[index])
		return -EINVAL;

	/* aggregate client constraints */
	list_for_each_entry(m, &pdata->client_list, node) {
		unsigned long constraint = m->constraint[index];
		unsigned long type = m->type[index];

		if (!constraint)
			continue;

		if (type == NVHOST_BW)
			bw_constraint += constraint;
		else if (type == NVHOST_PIXELRATE)
			pixelrate += constraint;
		else
			floor_rate = max(floor_rate, constraint);
	}

	/* use client specific aggregation if available */
	if (pdata->aggregate_constraints)
		rate = pdata->aggregate_constraints(dev, index, floor_rate,
						    pixelrate, bw_constraint);

	/* if frequency is not available, use default policy */
	if (!rate) {
		unsigned long bw_freq_khz =
			nvhost_emc_bw_to_freq_req(bw_constraint);
		rate = max(floor_rate, bw_freq_khz << 10);
	}

	/* take devfreq rate into account */
	rate = max(rate, pdata->clocks[index].devfreq_rate);

	/* if we still don't have any rate, use default */
	if (!rate)
		rate = pdata->clocks[index].default_rate;

	trace_nvhost_module_update_rate(dev->name, pdata->clocks[index].name,
					rate);

	ret = clk_set_rate(pdata->clk[index], rate);

	return ret;

}

int nvhost_module_set_rate(struct platform_device *dev, void *priv,
		unsigned long constraint, int index, unsigned long type)
{
	struct nvhost_module_client *m;
	int ret = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	nvhost_dbg_fn("%s", dev->name);

	mutex_lock(&client_list_lock);
	list_for_each_entry(m, &pdata->client_list, node) {
		if (m->priv == priv) {
			m->constraint[index] = constraint;
			m->type[index] = type;
		}
	}

	ret = nvhost_module_update_rate(dev, index);
	mutex_unlock(&client_list_lock);
	return ret;
}

int nvhost_module_add_client(struct platform_device *dev, void *priv)
{
	struct nvhost_module_client *client;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	nvhost_dbg_fn("%s num_clks=%d priv=%p", dev->name,
		      pdata->num_clks, priv);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	INIT_LIST_HEAD(&client->node);
	client->priv = priv;

	mutex_lock(&client_list_lock);
	list_add_tail(&client->node, &pdata->client_list);
	mutex_unlock(&client_list_lock);

	return 0;
}

void nvhost_module_remove_client(struct platform_device *dev, void *priv)
{
	int i;
	struct nvhost_module_client *m;
	int found = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	nvhost_dbg_fn("%s priv=%p", dev->name, priv);

	mutex_lock(&client_list_lock);
	list_for_each_entry(m, &pdata->client_list, node) {
		if (priv == m->priv) {
			list_del(&m->node);
			found = 1;
			break;
		}
	}
	if (found) {
		kfree(m);
		for (i = 0; i < pdata->num_clks; i++)
			nvhost_module_update_rate(dev, i);
	}
	mutex_unlock(&client_list_lock);
}

static ssize_t force_on_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int force_on = 0, ret = 0;
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr,
			     power_attr[NVHOST_POWER_SYSFS_ATTRIB_FORCE_ON]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	ret = sscanf(buf, "%d", &force_on);
	if (ret != 1)
		return -EINVAL;

	/* should we force the power on or off? */
	if (force_on && !pdata->forced_on) {
		/* force on */
		ret = nvhost_module_busy(dev);
		if (!ret)
			pdata->forced_on = true;
	} else if (!force_on && pdata->forced_on) {
		/* force off */
		nvhost_module_idle(dev);
		pdata->forced_on = false;
	}

	return count;
}

static ssize_t force_on_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr,
			     power_attr[NVHOST_POWER_SYSFS_ATTRIB_FORCE_ON]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	return sprintf(buf, "%d\n", pdata->forced_on);

}

static ssize_t powergate_delay_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int powergate_delay = 0, ret = 0;
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr, \
			power_attr[NVHOST_POWER_SYSFS_ATTRIB_POWERGATE_DELAY]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	if (!pdata->can_powergate) {
		dev_info(&dev->dev, "does not support power-gating\n");
		return count;
	}

	ret = sscanf(buf, "%d", &powergate_delay);
	if (ret == 1 && powergate_delay >= 0) {
		struct generic_pm_domain *genpd =
			pd_to_genpd(dev->dev.pm_domain);

		mutex_lock(&pdata->lock);
		pdata->powergate_delay = powergate_delay;
		mutex_unlock(&pdata->lock);

		pm_genpd_set_poweroff_delay(genpd, pdata->powergate_delay);
	} else {
		dev_err(&dev->dev, "Invalid powergate delay\n");
	}

	return count;
}

static ssize_t powergate_delay_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int ret;
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr, \
			power_attr[NVHOST_POWER_SYSFS_ATTRIB_POWERGATE_DELAY]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%d\n", pdata->powergate_delay);
	mutex_unlock(&pdata->lock);

	return ret;
}

static ssize_t clockgate_delay_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int clockgate_delay = 0, ret = 0;
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr, \
			power_attr[NVHOST_POWER_SYSFS_ATTRIB_CLOCKGATE_DELAY]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	ret = sscanf(buf, "%d", &clockgate_delay);
	if (ret == 1 && clockgate_delay >= 0) {
		mutex_lock(&pdata->lock);
		pdata->clockgate_delay = clockgate_delay;
		mutex_unlock(&pdata->lock);

		pm_runtime_set_autosuspend_delay(&dev->dev,
				pdata->clockgate_delay);
	} else {
		dev_err(&dev->dev, "Invalid clockgate delay\n");
	}

	return count;
}

static ssize_t clockgate_delay_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int ret;
	struct nvhost_device_power_attr *power_attribute =
		container_of(attr, struct nvhost_device_power_attr, \
			power_attr[NVHOST_POWER_SYSFS_ATTRIB_CLOCKGATE_DELAY]);
	struct platform_device *dev = power_attribute->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%d\n", pdata->clockgate_delay);
	mutex_unlock(&pdata->lock);

	return ret;
}

int nvhost_module_set_devfreq_rate(struct platform_device *dev, int index,
		unsigned long rate)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	int ret;

	rate = clk_round_rate(pdata->clk[index], rate);
	pdata->clocks[index].devfreq_rate = rate;

	trace_nvhost_module_set_devfreq_rate(dev->name,
			pdata->clocks[index].name, rate);

	mutex_lock(&client_list_lock);
	ret = nvhost_module_update_rate(dev, index);
	mutex_unlock(&client_list_lock);

	return ret;
}

int nvhost_clk_get(struct platform_device *dev, char *name, struct clk **clk)
{
	int i;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	for (i = 0; i < pdata->num_clks; i++) {
		if (strcmp(pdata->clocks[i].name, name) == 0) {
			*clk = pdata->clk[i];
			return 0;
		}
	}
	return -EINVAL;
}

int nvhost_module_init(struct platform_device *dev)
{
	int i = 0, err = 0;
	struct kobj_attribute *attr = NULL;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	/* initialize clocks to known state (=enabled) */
	pdata->num_clks = 0;
	INIT_LIST_HEAD(&pdata->client_list);

	if (pdata->virtual_dev) {
		pm_runtime_enable(&dev->dev);
		return err;
	}

	while (i < NVHOST_MODULE_MAX_CLOCKS && pdata->clocks[i].name) {
		char devname[MAX_DEVID_LENGTH];
		long rate = pdata->clocks[i].default_rate;
		struct clk *c;

		snprintf(devname, MAX_DEVID_LENGTH,
			 (dev->id <= 0) ? "tegra_%s" : "tegra_%s.%d",
			 dev->name, dev->id);
		c = clk_get_sys(devname, pdata->clocks[i].name);
		if (IS_ERR(c)) {
			dev_err(&dev->dev, "clk_get_sys failed for i=%d %s:%s",
				i, devname, pdata->clocks[i].name);
			/* arguably we should fail init here instead... */
			i++;
			continue;
		}
		nvhost_dbg_fn("%s->clk[%d] -> %s:%s:%p",
			      dev->name, pdata->num_clks,
			      devname, pdata->clocks[i].name,
			      c);
		rate = clk_round_rate(c, rate);
		clk_prepare_enable(c);
		clk_set_rate(c, rate);
		pdata->clk[pdata->num_clks++] = c;
		i++;
	}
	pdata->num_clks = i;

	/* reset the module */
	mutex_lock(&pdata->lock);
	do_module_reset_locked(dev);
	mutex_unlock(&pdata->lock);

	/* disable the clocks */
	for (i = 0; i < pdata->num_clks; ++i)
		clk_disable_unprepare(pdata->clk[i]);

	/* disable railgating if pm runtime is not available */
	pdata->can_powergate = IS_ENABLED(CONFIG_PM_RUNTIME) &&
		IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS) &&
		pdata->can_powergate;

	/* needed to WAR MBIST issue */
	if (pdata->poweron_toggle_slcg) {
		pdata->toggle_slcg_notifier.notifier_call =
			&nvhost_module_toggle_slcg;
		if (pdata->powergate_ids[0] != -1)
			slcg_register_notifier(pdata->powergate_ids[0],
					       &pdata->toggle_slcg_notifier);
		if (pdata->powergate_ids[1] != -1)
			slcg_register_notifier(pdata->powergate_ids[1],
					       &pdata->toggle_slcg_notifier);
	}

	/* Ensure that the above or device specific MBIST WAR gets applied */
	if (pdata->poweron_toggle_slcg || pdata->slcg_notifier_enable) {
		do_powergate_locked(pdata->powergate_ids[0]);
		do_powergate_locked(pdata->powergate_ids[1]);
		do_unpowergate_locked(pdata->powergate_ids[0]);
		do_unpowergate_locked(pdata->powergate_ids[1]);
	}

	/* power gate units that we can power gate */
	if (pdata->can_powergate) {
		do_powergate_locked(pdata->powergate_ids[0]);
		do_powergate_locked(pdata->powergate_ids[1]);
	} else {
		do_unpowergate_locked(pdata->powergate_ids[0]);
		do_unpowergate_locked(pdata->powergate_ids[1]);
	}

	/* set pm runtime delays */
	if (pdata->clockgate_delay) {
		pm_runtime_set_autosuspend_delay(&dev->dev,
			pdata->clockgate_delay);
		pm_runtime_use_autosuspend(&dev->dev);
	}

	/* initialize no_poweroff_req_mutex */
	mutex_init(&pdata->no_poweroff_req_mutex);

	/* turn on pm runtime */
	pm_runtime_enable(&dev->dev);
	if (!pm_runtime_enabled(&dev->dev))
		nvhost_module_enable_clk(&dev->dev);

	/* if genpd is not available, the domain is powered already.
	 * just ensure that we load the gating registers now */
	if (!(IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS) &&
	      IS_ENABLED(CONFIG_PM_RUNTIME))) {
		nvhost_module_enable_clk(&dev->dev);
		nvhost_module_load_regs(dev, pdata->engine_can_cg);
		nvhost_module_disable_clk(&dev->dev);
	}

	/* Init the power sysfs attributes for this device */
	pdata->power_attrib = devm_kzalloc(&dev->dev,
		sizeof(struct nvhost_device_power_attr), GFP_KERNEL);
	if (!pdata->power_attrib) {
		dev_err(&dev->dev, "Unable to allocate sysfs attributes\n");
		return -ENOMEM;
	}
	pdata->power_attrib->ndev = dev;

	pdata->power_kobj = kobject_create_and_add("acm", &dev->dev.kobj);
	if (!pdata->power_kobj) {
		dev_err(&dev->dev, "Could not add dir 'power'\n");
		err = -EIO;
		goto fail_attrib_alloc;
	}

	attr = &pdata->power_attrib->power_attr[NVHOST_POWER_SYSFS_ATTRIB_CLOCKGATE_DELAY];
	attr->attr.name = "clockgate_delay";
	attr->attr.mode = S_IWUSR | S_IRUGO;
	attr->show = clockgate_delay_show;
	attr->store = clockgate_delay_store;
	sysfs_attr_init(&attr->attr);
	if (sysfs_create_file(pdata->power_kobj, &attr->attr)) {
		dev_err(&dev->dev, "Could not create sysfs attribute clockgate_delay\n");
		err = -EIO;
		goto fail_clockdelay;
	}

	attr = &pdata->power_attrib->power_attr[NVHOST_POWER_SYSFS_ATTRIB_POWERGATE_DELAY];
	attr->attr.name = "powergate_delay";
	attr->attr.mode = S_IWUSR | S_IRUGO;
	attr->show = powergate_delay_show;
	attr->store = powergate_delay_store;
	sysfs_attr_init(&attr->attr);
	if (sysfs_create_file(pdata->power_kobj, &attr->attr)) {
		dev_err(&dev->dev, "Could not create sysfs attribute powergate_delay\n");
		err = -EIO;
		goto fail_powergatedelay;
	}

	attr = &pdata->power_attrib->power_attr[NVHOST_POWER_SYSFS_ATTRIB_FORCE_ON];
	attr->attr.name = "force_on";
	attr->attr.mode = S_IWUSR | S_IRUGO;
	attr->show = force_on_show;
	attr->store = force_on_store;
	sysfs_attr_init(&attr->attr);
	if (sysfs_create_file(pdata->power_kobj, &attr->attr)) {
		dev_err(&dev->dev, "Could not create sysfs attribute force_on\n");
		err = -EIO;
		goto fail_forceon;
	}

	return 0;

fail_forceon:
	attr = &pdata->power_attrib->power_attr[NVHOST_POWER_SYSFS_ATTRIB_POWERGATE_DELAY];
	sysfs_remove_file(pdata->power_kobj, &attr->attr);

fail_powergatedelay:
	attr = &pdata->power_attrib->power_attr[NVHOST_POWER_SYSFS_ATTRIB_CLOCKGATE_DELAY];
	sysfs_remove_file(pdata->power_kobj, &attr->attr);

fail_clockdelay:
	kobject_put(pdata->power_kobj);

fail_attrib_alloc:
	kfree(pdata->power_attrib);

	return err;
}
EXPORT_SYMBOL(nvhost_module_init);

void nvhost_module_deinit(struct platform_device *dev)
{
	int i;
	struct kobj_attribute *attr = NULL;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);

	devfreq_suspend_device(pdata->power_manager);

	if (pdata->prepare_poweroff)
		pdata->prepare_poweroff(dev);

	if (!pm_runtime_enabled(&dev->dev))
		nvhost_module_disable_clk(&dev->dev);
	else
		pm_runtime_disable(&dev->dev);

	for (i = 0; i < pdata->num_clks; i++)
		clk_put(pdata->clk[i]);

	if (pdata->power_kobj) {
		for (i = 0; i < NVHOST_POWER_SYSFS_ATTRIB_MAX; i++) {
			attr = &pdata->power_attrib->power_attr[i];
			sysfs_remove_file(pdata->power_kobj, &attr->attr);
		}

		kobject_put(pdata->power_kobj);
	}
}

const struct dev_pm_ops nvhost_module_pm_ops = {
#if defined(CONFIG_PM_RUNTIME) && !defined(CONFIG_PM_GENERIC_DOMAINS)
	.runtime_suspend = nvhost_module_disable_clk,
	.runtime_resume = nvhost_module_enable_clk,
#endif
};
EXPORT_SYMBOL(nvhost_module_pm_ops);

/*FIXME Use API to get host1x domain */
static struct generic_pm_domain *host1x_domain;

static int _nvhost_module_add_domain(struct generic_pm_domain *domain,
	struct platform_device *pdev, bool client)
{
	int ret = 0;

#ifdef CONFIG_PM_GENERIC_DOMAINS
	struct nvhost_device_data *pdata;
	struct dev_power_governor *pm_domain_gov = NULL;

	pdata = platform_get_drvdata(pdev);
	if (!pdata)
		return -EINVAL;

	if (!pdata->can_powergate)
		pm_domain_gov = &pm_domain_always_on_gov;

	if (__pm_genpd_name_add_device(domain->name, &pdev->dev, NULL)) {
		pm_genpd_init(domain, pm_domain_gov, true);

		domain->power_off = nvhost_module_power_off;
		domain->power_on = nvhost_module_power_on;
		domain->dev_ops.start = nvhost_module_enable_clk;
		domain->dev_ops.stop = nvhost_module_disable_clk;
		domain->dev_ops.save_state = nvhost_module_prepare_poweroff;
		domain->dev_ops.restore_state = nvhost_module_finalize_poweron;
		if (client) {
#warning TODO: pm domain suspend ops removed
#if 0
			domain->dev_ops.suspend = nvhost_module_suspend;
			domain->dev_ops.resume = nvhost_module_finalize_poweron;
#endif
		}

		/* Set only host1x as wakeup capable */
		device_set_wakeup_capable(&pdev->dev, !client);
		ret = pm_genpd_add_device(domain, &pdev->dev);
		if (pdata->powergate_delay)
			pm_genpd_set_poweroff_delay(domain,
					pdata->powergate_delay);
		if (client)
			pm_genpd_add_subdomain(host1x_domain, domain);
		else {
			tegra_pd_add_sd(domain);
			host1x_domain = domain;
		}
	}
#endif

	return ret;
}

void nvhost_register_client_domain(struct generic_pm_domain *domain)
{
	pm_genpd_add_subdomain(host1x_domain, domain);
}
EXPORT_SYMBOL(nvhost_register_client_domain);

void nvhost_unregister_client_domain(struct generic_pm_domain *domain)
{
	pm_genpd_remove_subdomain(host1x_domain, domain);
}
EXPORT_SYMBOL(nvhost_unregister_client_domain);

/* common runtime pm and power domain APIs */
int nvhost_module_add_domain(struct generic_pm_domain *domain,
	struct platform_device *pdev)
{
	if (!strcmp(domain->name, "tegra-host1x"))
		return _nvhost_module_add_domain(domain, pdev, 0);
	else
		return _nvhost_module_add_domain(domain, pdev, 1);
}
EXPORT_SYMBOL(nvhost_module_add_domain);

int nvhost_module_enable_clk(struct device *dev)
{
	int index = 0;
	struct nvhost_device_data *pdata;

	/* enable parent's clock if required */
	if (dev->parent && dev->parent != &platform_bus)
		nvhost_module_enable_clk(dev->parent);

	pdata = dev_get_drvdata(dev);
	if (!pdata)
		return -EINVAL;

	for (index = 0; index < pdata->num_clks; index++) {
		int err = clk_prepare_enable(pdata->clk[index]);
		if (err) {
			dev_err(dev, "Cannot turn on clock %s",
				pdata->clocks[index].name);
			return -EINVAL;
		}
	}

	trace_nvhost_module_enable_clk(pdata->pdev->name,
					pdata->num_clks);

	return 0;
}
EXPORT_SYMBOL(nvhost_module_enable_clk);

int nvhost_module_disable_clk(struct device *dev)
{
	int index = 0;
	struct nvhost_device_data *pdata;

	pdata = dev_get_drvdata(dev);
	if (!pdata)
		return -EINVAL;

	trace_nvhost_module_disable_clk(pdata->pdev->name,
					pdata->num_clks);

	for (index = 0; index < pdata->num_clks; index++)
		clk_disable_unprepare(pdata->clk[index]);

	/* disable parent's clock if required */
	if (dev->parent && dev->parent != &platform_bus)
		nvhost_module_disable_clk(dev->parent);

	return 0;
}
EXPORT_SYMBOL(nvhost_module_disable_clk);

static void nvhost_module_load_regs(struct platform_device *pdev, bool prod)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvhost_gating_register *regs = pdata->engine_cg_regs;

	if (!regs)
		return;

	while (regs->addr) {
		if (prod)
			host1x_writel(pdev, regs->addr, regs->prod);
		else
			host1x_writel(pdev, regs->addr, regs->disable);
		regs++;
	}
}

#ifdef CONFIG_PM_GENERIC_DOMAINS
#if 0
static int nvhost_module_suspend(struct device *dev)
{
	/*
	 * device_prepare takes one ref, so expect usage count to
	 * be 1 at this point.
	 */
#ifdef CONFIG_PM_RUNTIME
	if (atomic_read(&dev->power.usage_count) > 1)
		return -EBUSY;
#endif

	return nvhost_module_prepare_poweroff(dev);
}
#endif

static int nvhost_module_power_on(struct generic_pm_domain *domain)
{
	struct nvhost_device_data *pdata;

	pdata = container_of(domain, struct nvhost_device_data, pd);

	mutex_lock(&pdata->lock);
	if (pdata->can_powergate) {
		trace_nvhost_module_power_on(pdata->pdev->name,
			pdata->powergate_ids[0], pdata->powergate_ids[1]);
		do_unpowergate_locked(pdata->powergate_ids[0]);
		do_unpowergate_locked(pdata->powergate_ids[1]);
	}

	mutex_unlock(&pdata->lock);

	return 0;
}

static int nvhost_module_power_off(struct generic_pm_domain *domain)
{
	struct nvhost_device_data *pdata;

	pdata = container_of(domain, struct nvhost_device_data, pd);

	mutex_lock(&pdata->lock);
	if (pdata->can_powergate) {
		trace_nvhost_module_power_off(pdata->pdev->name,
			pdata->powergate_ids[0], pdata->powergate_ids[1]);
		do_powergate_locked(pdata->powergate_ids[0]);
		do_powergate_locked(pdata->powergate_ids[1]);
	}
	mutex_unlock(&pdata->lock);

	return 0;
}

static int nvhost_module_prepare_poweroff(struct device *dev)
{
	struct nvhost_device_data *pdata;

	pdata = dev_get_drvdata(dev);
	if (!pdata)
		return -EINVAL;

	devfreq_suspend_device(pdata->power_manager);
	nvhost_scale_hw_deinit(to_platform_device(dev));

	if (pdata->prepare_poweroff)
		pdata->prepare_poweroff(to_platform_device(dev));

	return 0;
}

static int nvhost_module_finalize_poweron(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata;
	int retry_count, ret = 0;

	pdata = dev_get_drvdata(dev);
	if (!pdata)
		return -EINVAL;

	/* WAR to bug 1588951: Retry booting 3 times */

	for (retry_count = 0; retry_count < 3; retry_count++) {
		if (!pdata->poweron_toggle_slcg) {
			if (pdata->poweron_reset)
				nvhost_module_reset(pdev, false);

			/* Load clockgating registers */
			nvhost_module_load_regs(pdev, pdata->engine_can_cg);
		} else {
			/* If poweron_toggle_slcg is set, following is already
			 * executed once. Skip to avoid doing it twice. */
			if (retry_count > 0) {
				/* First, reset module */
				nvhost_module_reset(pdev, false);

				/* Disable SLCG, wait and re-enable it */
				nvhost_module_load_regs(pdev, false);
				udelay(1);
				if (pdata->engine_can_cg)
					nvhost_module_load_regs(pdev, true);
			}
		}

		if (pdata->finalize_poweron)
			ret = pdata->finalize_poweron(to_platform_device(dev));

		/* Exit loop if we pass module specific initialization */
		if (!ret)
			break;
	}

	nvhost_scale_hw_init(to_platform_device(dev));
	devfreq_resume_device(pdata->power_manager);

	return ret;
}
#endif

static int nvhost_module_toggle_slcg(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct nvhost_device_data *pdata =
		container_of(nb, struct nvhost_device_data,
			     toggle_slcg_notifier);
	struct platform_device *pdev = pdata->pdev;

	/* First, reset the engine */
	do_module_reset_locked(pdev);

	/* Then, disable slcg, wait a while and re-enable it */
	nvhost_module_load_regs(pdev, false);
	udelay(1);
	if (pdata->engine_can_cg)
		nvhost_module_load_regs(pdev, true);

	return 0;
}

/* public host1x power management APIs */
bool nvhost_module_powered_ext(struct platform_device *dev)
{
	if (dev->dev.parent && dev->dev.parent != &platform_bus)
		dev = to_platform_device(dev->dev.parent);
	return nvhost_module_powered(dev);
}
EXPORT_SYMBOL(nvhost_module_powered_ext);

int nvhost_module_busy_ext(struct platform_device *dev)
{
	if (dev->dev.parent && dev->dev.parent != &platform_bus)
		dev = to_platform_device(dev->dev.parent);
	return nvhost_module_busy(dev);
}
EXPORT_SYMBOL(nvhost_module_busy_ext);

void nvhost_module_idle_ext(struct platform_device *dev)
{
	if (dev->dev.parent && dev->dev.parent != &platform_bus)
		dev = to_platform_device(dev->dev.parent);
	nvhost_module_idle(dev);
}
EXPORT_SYMBOL(nvhost_module_idle_ext);
