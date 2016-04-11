/*
 * Driver for Tegra Security Engine
 *
 * Copyright (c) 2015-2016, NVIDIA Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _CRYPTO_TEGRA_SE_H
#define _CRYPTO_TEGRA_SE_H

#include <crypto/hash.h>
#include <crypto/sha.h>

#define PFX	"tegra-se-nvhost: "

#define TEGRA_SE_CRA_PRIORITY	300
#define TEGRA_SE_COMPOSITE_PRIORITY 400
#define TEGRA_SE_CRYPTO_QUEUE_LENGTH 50
#define SE_MAX_SRC_SG_COUNT		50
#define SE_MAX_DST_SG_COUNT		50

#define TEGRA_SE_KEYSLOT_COUNT		16
#define SE_MAX_LAST_BLOCK_SIZE	0xFFFFF

/* SE register definitions */
#define SE1_AES0_CONFIG_REG_OFFSET		0x204
#define SE2_AES1_CONFIG_REG_OFFSET		0x404

#define SE_AES_CRYPTO_CONFIG_OFFSET		0x4
#define SE_AES_IN_ADDR_OFFSET			0x8
#define SE_AES_IN_ADDR_HI_OFFSET		0xC
#define SE_AES_OUT_ADDR_OFFSET			0x10
#define SE_AES_OUT_ADDR_HI_OFFSET		0x14
#define SE_AES_CRYPTO_LINEAR_CTR		0x18
#define SE_AES_CRYPTO_LAST_BLOCK_OFFSET		0x28
#define SE_AES_OPERATION_OFFSET			0x34
#define SE_AES_CRYPTO_KEYTABLE_ADDR_OFFSET		0xB8
#define SE_AES_CRYPTO_KEYTABLE_DATA_OFFSET		0xBC


#define SE_CONFIG_ENC_ALG_SHIFT		12
#define SE_CONFIG_DEC_ALG_SHIFT		8
#define ALG_AES_ENC		1
#define ALG_RNG			2
#define ALG_SHA			3
#define ALG_RSA			4
#define ALG_NOP			0
#define ALG_AES_DEC		1
#define ALG_KEYFETCH		5
#define SE_CONFIG_ENC_ALG(x)		(x << SE_CONFIG_ENC_ALG_SHIFT)
#define SE_CONFIG_DEC_ALG(x)		(x << SE_CONFIG_DEC_ALG_SHIFT)
#define SE_CONFIG_DST_SHIFT			2
#define DST_MEMORY		0
#define DST_HASHREG		1
#define DST_KEYTAB		2
#define DST_SRK			3
#define DST_RSAREG		4
#define SE_CONFIG_DST(x)			(x << SE_CONFIG_DST_SHIFT)
#define SE_CONFIG_ENC_MODE_SHIFT	24
#define SE_CONFIG_DEC_MODE_SHIFT	16
#define MODE_KEY128		0
#define MODE_KEY192		1
#define MODE_KEY256		2
#define MODE_SHA1		0
#define MODE_SHA224		4
#define MODE_SHA256		5
#define MODE_SHA384		6
#define MODE_SHA512		7
#define SE_CONFIG_ENC_MODE(x)		(x << SE_CONFIG_ENC_MODE_SHIFT)
#define SE_CONFIG_DEC_MODE(x)		(x << SE_CONFIG_DEC_MODE_SHIFT)

#define SE_RNG_CONFIG_REG_OFFSET		0x234
#define DRBG_MODE_SHIFT	0
#define DRBG_MODE_NORMAL	0
#define DRBG_MODE_FORCE_INSTANTION	1
#define DRBG_MODE_FORCE_RESEED		2
#define SE_RNG_CONFIG_MODE(x)		(x << DRBG_MODE_SHIFT)

#define SE_RNG_SRC_CONFIG_REG_OFFSET	0x2d8
#define DRBG_RO_ENT_SRC_SHIFT	1
#define DRBG_RO_ENT_SRC_ENABLE	1
#define DRBG_RO_ENT_SRC_DISABLE	0
#define SE_RNG_SRC_CONFIG_RO_ENT_SRC(x)	(x << DRBG_RO_ENT_SRC_SHIFT)
#define DRBG_RO_ENT_SRC_LOCK_SHIFT	0
#define DRBG_RO_ENT_SRC_LOCK_ENABLE	1
#define DRBG_RO_ENT_SRC_LOCK_DISABLE	0
#define SE_RNG_SRC_CONFIG_RO_ENT_SRC_LOCK(x) (x << DRBG_RO_ENT_SRC_LOCK_SHIFT)

#define DRBG_SRC_SHIFT	2
#define DRBG_SRC_NONE	0
#define DRBG_SRC_ENTROPY	1
#define DRBG_SRC_LFSR	2
#define SE_RNG_CONFIG_SRC(x)		(x << DRBG_SRC_SHIFT)

#define SE_RNG_RESEED_INTERVAL_REG_OFFSET		0x2dc

#define SE_KEYTABLE_REG_OFFSET		0x31c
#define SE_KEYTABLE_SLOT_SHIFT		4
#define SE_KEYTABLE_SLOT(x)			(x << SE_KEYTABLE_SLOT_SHIFT)
#define SE_KEYTABLE_QUAD_SHIFT		2
#define QUAD_KEYS_128		0
#define QUAD_KEYS_192		1
#define QUAD_KEYS_256		1
#define QUAD_ORG_IV		2
#define QUAD_UPDTD_IV		3
#define SE_KEYTABLE_QUAD(x)			(x << SE_KEYTABLE_QUAD_SHIFT)
#define SE_KEYTABLE_OP_TYPE_SHIFT	9
#define OP_READ			0
#define OP_WRITE		1
#define SE_KEYTABLE_OP_TYPE(x)		(x << SE_KEYTABLE_OP_TYPE_SHIFT)
#define SE_KEYTABLE_TABLE_SEL_SHIFT		8
#define TABLE_KEYIV		0
#define TABLE_SCHEDULE	1
#define SE_KEYTABLE_TABLE_SEL(x)	(x << SE_KEYTABLE_TABLE_SEL_SHIFT)
#define SE_KEYTABLE_PKT_SHIFT		0
#define SE_KEYTABLE_PKT(x)			(x << SE_KEYTABLE_PKT_SHIFT)

#define SE_OP_DONE_SHIFT	4
#define OP_DONE	1
#define SE_OP_DONE(x, y)	((x) && (y << SE_OP_DONE_SHIFT))

#define SE_CRYPTO_HASH_SHIFT		0
#define HASH_DISABLE	0
#define HASH_ENABLE		1
#define SE_CRYPTO_HASH(x)			(x << SE_CRYPTO_HASH_SHIFT)

#define SE4_SHA_TASK_CONFIG		0x108
#define HW_INIT_HASH_DISABLE	0
#define HW_INIT_HASH_ENABLE	1
#define SE4_HW_INIT_HASH_SHIFT	0
#define SE4_HW_INIT_HASH(x)			(x << SE4_HW_INIT_HASH_SHIFT)

#define SE_CRYPTO_XOR_POS_SHIFT		1
#define XOR_BYPASS		0
#define XOR_TOP			2
#define XOR_BOTTOM		3
#define SE_CRYPTO_XOR_POS(x)		(x << SE_CRYPTO_XOR_POS_SHIFT)
#define SE_CRYPTO_INPUT_SEL_SHIFT	3
#define INPUT_MEMORY		0
#define INPUT_RANDOM		1
#define INPUT_AESOUT		2
#define INPUT_LNR_CTR		3
#define SE_CRYPTO_INPUT_SEL(x)		(x << SE_CRYPTO_INPUT_SEL_SHIFT)
#define SE_CRYPTO_VCTRAM_SEL_SHIFT	5
#define VCTRAM_MEMORY		0
#define VCTRAM_AESOUT		2
#define VCTRAM_PREVAHB		3
#define SE_CRYPTO_VCTRAM_SEL(x)		(x << SE_CRYPTO_VCTRAM_SEL_SHIFT)
#define SE_CRYPTO_IV_SEL_SHIFT		7
#define IV_ORIGINAL		0
#define IV_UPDATED		1
#define IV_REG			2
#define SE_CRYPTO_IV_SEL(x)			(x << SE_CRYPTO_IV_SEL_SHIFT)
#define SE_CRYPTO_CORE_SEL_SHIFT	9
#define CORE_DECRYPT	0
#define CORE_ENCRYPT	1
#define SE_CRYPTO_CORE_SEL(x)		(x << SE_CRYPTO_CORE_SEL_SHIFT)
#define SE_CRYPTO_KEY_INDEX_SHIFT	24
#define SE_CRYPTO_KEY_INDEX(x)		(x << SE_CRYPTO_KEY_INDEX_SHIFT)
#define SE_CRYPTO_CTR_CNTN_SHIFT	11
#define SE_CRYPTO_CTR_CNTN(x)		(x << SE_CRYPTO_CTR_CNTN_SHIFT)

#define SE_CRYPTO_CTR_REG_COUNT		4

#define OP_START		1
#define OP_RESTART_OUT		2
#define OP_CTX_SAVE		3
#define OP_RESTART_IN		4
#define OP_RESTART_INOUT	5
#define OP_DUMMY		6
#define SE_OPERATION_OP_SHIFT		0
#define SE_OPERATION_OP(x)		(x << SE_OPERATION_OP_SHIFT)

#define SE_OPERATION_LASTBUF_SHIFT	16
#define SE_OPERATION_LASTBUF(x)		(x << SE_OPERATION_LASTBUF_SHIFT)
#define LASTBUF_TRUE		1
#define LASTBUF_FALSE		0

#define SE_OPERATION_WRSTALL_SHIFT	15
#define SE_OPERATION_WRSTALL(x)		(x << SE_OPERATION_WRSTALL_SHIFT)
#define WRSTALL_TRUE		1
#define WRSTALL_FALSE		0

#define SE_ADDR_HI_MSB_SHIFT		24
#define SE_ADDR_HI_SZ_SHIFT		0
#define SE_ADDR_HI_MSB(x)		(x << SE_ADDR_HI_MSB_SHIFT)
#define MSB(x)				((x & 0xFF00000000) >> 32)
#define SE_ADDR_HI_SZ(x)		(x << SE_ADDR_HI_SZ_SHIFT)

#define SE_MAX_TASKS_PER_SUBMIT		32
#define SE_MAX_SUBMIT_CHAIN_SZ		40
#define SE_WORD_SIZE_BYTES		4

#define SE_INT_ENABLE_REG_OFFSET	0x88
#define SE1_INT_ENABLE_SHIFT		1
#define SE1_INT_ENABLE(x)		(x << SE1_INT_ENABLE_SHIFT)
#define SE2_INT_ENABLE_SHIFT		0
#define SE2_INT_ENABLE(x)		(x << SE2_INT_ENABLE_SHIFT)
#define SE3_INT_ENABLE_SHIFT		2
#define SE3_INT_ENABLE(x)		(x << SE3_INT_ENABLE_SHIFT)
#define SE4_INT_ENABLE_SHIFT		3
#define SE4_INT_ENABLE(x)		(x << SE4_INT_ENABLE_SHIFT)

#define INT_DISABLE		0
#define INT_ENABLE		1

#define SE1_AES0_INT_ENABLE_OFFSET	0x2EC
#define SE2_AES1_INT_ENABLE_OFFSET	0x4EC
#define SE3_RSA_INT_ENABLE_OFFSET	0x754
#define SE4_SHA_INT_ENABLE_OFFSET	0x180

#define SE1_AES0_INT_STATUS_REG_OFFSET	0x2F0
#define SE2_AES1_INT_STATUS_REG_OFFSET	0x4F0
#define SE3_RSA_INT_STATUS_REG_OFFSET	0x758
#define SE4_SHA_INT_STATUS_REG_OFFSET	0x184

#define SE_CRYPTO_KEYTABLE_DST_REG_OFFSET	0X330
#define SE_CRYPTO_KEYTABLE_DST_WORD_QUAD_SHIFT		0
#define SE_CRYPTO_KEYTABLE_DST_WORD_QUAD(x)		\
		(x << SE_CRYPTO_KEYTABLE_DST_WORD_QUAD_SHIFT)

#define SE_KEYTABLE_QUAD_SIZE_BYTES	16

#define SE_SPARE_0_REG_OFFSET		0x80c

#define SE4_SHA_CONFIG_REG_OFFSET	0x104
#define SE_SHA_MSG_LENGTH_OFFSET	0x18
#define SE_SHA_OPERATION_OFFSET		0x78

#define SHA_DISABLE		0
#define SHA_ENABLE		1

#define SE_HASH_RESULT_REG_OFFSET	0x13c
#define SE_CMAC_RESULT_REG_OFFSET	0x4c4

#define TEGRA_SE_KEY_256_SIZE		32
#define TEGRA_SE_KEY_192_SIZE		24
#define TEGRA_SE_KEY_128_SIZE		16
#define TEGRA_SE_AES_BLOCK_SIZE		16
#define TEGRA_SE_AES_MIN_KEY_SIZE	16
#define TEGRA_SE_AES_MAX_KEY_SIZE	32
#define TEGRA_SE_AES_IV_SIZE		16
#define TEGRA_SE_RNG_IV_SIZE		16
#define TEGRA_SE_RNG_DT_SIZE		16
#define TEGRA_SE_RNG_KEY_SIZE		16
#define TEGRA_SE_RNG_SEED_SIZE		(TEGRA_SE_RNG_IV_SIZE + \
						TEGRA_SE_RNG_KEY_SIZE + \
						TEGRA_SE_RNG_DT_SIZE)
#define TEGRA_SE_AES_CMAC_DIGEST_SIZE	16
#define TEGRA_SE_RSA512_DIGEST_SIZE	64
#define TEGRA_SE_RSA1024_DIGEST_SIZE	128
#define TEGRA_SE_RSA1536_DIGEST_SIZE	192
#define TEGRA_SE_RSA2048_DIGEST_SIZE	256

#define TEGRA_SE_RSA_KEYSLOT_COUNT	4
#define SE_RSA_OUTPUT			0x628

#define RSA_KEY_SLOT_ONE	0
#define RSA_KEY_SLOT_TW0	1
#define RSA_KEY_SLOT_THREE	2
#define RSA_KEY_SLOT_FOUR	3
#define RSA_KEY_NUM_SHIFT	23
#define RSA_KEY_NUM(x)	(x << RSA_KEY_NUM_SHIFT)

#define RSA_KEY_TYPE_EXP	0
#define RSA_KEY_TYPE_MOD	1
#define RSA_KEY_TYPE_SHIFT	6
#define RSA_KEY_TYPE(x)	(x << RSA_KEY_TYPE_SHIFT)

#define RSA_KEY_SLOT_SHIFT	24
#define RSA_KEY_SLOT(x)	(x << RSA_KEY_SLOT_SHIFT)

#define SE3_RSA_CONFIG_REG_OFFSET	0x604
#define SE_RSA_OPERATION_OFFSET		0x20
#define SE_RSA_KEYTABLE_ADDR_OFFSET	0x148
#define SE_RSA_KEYTABLE_DATA_OFFSET	0x14C

#define RSA_KEY_PKT_WORD_ADDR_SHIFT	0
#define RSA_KEY_PKT_WORD_ADDR(x)	(x << RSA_KEY_PKT_WORD_ADDR_SHIFT)

#define SE_RSA_KEYTABLE_PKT_SHIFT	0
#define SE_RSA_KEYTABLE_PKT(x)	(x << SE_RSA_KEYTABLE_PKT_SHIFT)

#define SE_MAGIC_PATTERN	0x4E56
#define SE_SLOT_NUM_MASK	0xF000
#define SE_SLOT_POSITION	12
#define SE_KEY_LEN_MASK	0x3FF
#define SE_MAGIC_PATTERN_OFFSET	16
#endif /* _CRYPTO_TEGRA_SE_H */
