/*
 * TP-LINK TL-WR2041N v2 board support
 *
 * Copyright (c) 2013 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2016 Joshua Li <sunrisela@gmail.com>
 *
 * Based on the Qualcomm Atheros AP135/AP136 reference board support code
 *   Copyright (c) 2012 Qualcomm Atheros
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/ar8216_platform.h>

#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-spi.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define TL_WR2041_V2_GPIO_LED_WLAN      12
#define TL_WR2041_V2_GPIO_LED_WPS        9
#define TL_WR2041_V2_GPIO_LED_SYSTEM    19
#define TL_WR2041_V2_GPIO_LED_TURBO     11

#define TL_WR2041_V2_GPIO_BTN_RESET	    17
#define TL_WR2041_V2_GPIO_BTN_TURBO     16

#define TL_WR2041_V2_KEYS_POLL_INTERVAL	20	/* msecs */
#define TL_WR2041_V2_KEYS_DEBOUNCE_INTERVAL (3 * TL_WR2041_V2_KEYS_POLL_INTERVAL)

#define ATH_MII_MGMT_CMD        0x24
#define ATH_MGMT_CMD_READ       0x1

#define ATH_MII_MGMT_ADDRESS    0x28
#define ATH_ADDR_SHIFT          8

#define ATH_MII_MGMT_CTRL       0x2c
#define ATH_MII_MGMT_STATUS     0x30

#define ATH_MII_MGMT_IND        0x34
#define ATH_MGMT_IND_BUSY       (1 << 0)
#define ATH_MGMT_IND_INVALID    (1 << 2)

#define QCA955X_ETH_CFG_GE0_MII_EN      BIT(1)
#define QCA955X_ETH_CFG_GE0_MII_SLAVE   BIT(4)

static const char *wr2041n_v2_part_probes[] = {
	"tp-link",
	NULL,
};

static struct flash_platform_data wr2041n_v2_flash_data = {
	.part_probes	= wr2041n_v2_part_probes,
};

static struct gpio_led tl_wr2041_v2_leds_gpio[] __initdata = {
	{
		.name		= "tp-link:green:wps",
		.gpio		= TL_WR2041_V2_GPIO_LED_WPS,
		.active_low	= 1,
	},
	{
		.name		= "tp-link:green:system",
		.gpio		= TL_WR2041_V2_GPIO_LED_SYSTEM,
		.active_low	= 1,
	},
	{
		.name		= "tp-link:green:wlan",
		.gpio		= TL_WR2041_V2_GPIO_LED_WLAN,
		.active_low	= 1,
	},
};

static struct gpio_keys_button tl_wr2041_v2_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = TL_WR2041_V2_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= TL_WR2041_V2_GPIO_BTN_RESET,
		.active_low	= 1,
	},
    {
        .desc       = "RFKILL button",
        .type       = EV_KEY,
        .code       = KEY_RFKILL,
        .debounce_interval = TL_WR2041_V2_KEYS_DEBOUNCE_INTERVAL,
        .gpio       = TL_WR2041_V2_GPIO_BTN_TURBO,
        .active_low = 1,
    },
};

static struct mdio_board_info wr2041n_v2_mdio0_info[] = {
	{
		.bus_id = "ag71xx-mdio.0",
		.phy_addr = 0,
		.platform_data = NULL,
	},
};

static unsigned long __init ath_gmac_reg_rd(unsigned long reg)
{
    void __iomem *base;
    unsigned long t;

    base = ioremap(AR71XX_GE0_BASE, AR71XX_GE0_SIZE);

    t = __raw_readl(base + reg);

    iounmap(base);

    return t;
}

static void __init ath_gmac_reg_wr(unsigned long reg, unsigned long value)
{
    void __iomem *base;
    unsigned long t = value;

    base = ioremap(AR71XX_GE0_BASE, AR71XX_GE0_SIZE);

    __raw_writel(t, base + reg);

    iounmap(base);
}

static void __init phy_reg_write(unsigned char phy_addr, unsigned char reg, unsigned short data)
{
    unsigned short addr = (phy_addr << ATH_ADDR_SHIFT) | reg;
    volatile int rddata;
    unsigned short ii = 0xFFFF;

    do  
    {   
        udelay(5);
        rddata = ath_gmac_reg_rd(ATH_MII_MGMT_IND) & 0x1;
    } while (rddata && --ii);

    ath_gmac_reg_wr(ATH_MII_MGMT_ADDRESS, addr);
    ath_gmac_reg_wr(ATH_MII_MGMT_CTRL, data);

    do
    {
        udelay(5);
        rddata = ath_gmac_reg_rd(ATH_MII_MGMT_IND) & 0x1;
    } while (rddata && --ii);
}

static unsigned short __init phy_reg_read(unsigned char phy_addr, unsigned char reg)
{
    unsigned short addr = (phy_addr << ATH_ADDR_SHIFT) | reg, val;
    volatile int rddata;
    unsigned short ii = 0xffff;

    do
    {
        udelay(5);
        rddata = ath_gmac_reg_rd(ATH_MII_MGMT_IND) & 0x1;
    } while (rddata && --ii);

    ath_gmac_reg_wr(ATH_MII_MGMT_CMD, 0x0);
    ath_gmac_reg_wr(ATH_MII_MGMT_ADDRESS, addr);
    ath_gmac_reg_wr(ATH_MII_MGMT_CMD, ATH_MGMT_CMD_READ);

    do
    {
        udelay(5);
        rddata = ath_gmac_reg_rd(ATH_MII_MGMT_IND) & 0x1;
    } while (rddata && --ii);

    val = ath_gmac_reg_rd(ATH_MII_MGMT_STATUS);
    ath_gmac_reg_wr(ATH_MII_MGMT_CMD, 0x0);

    return val;
}

static void __init athrs27_reg_write(unsigned int s27_addr, unsigned int s27_write_data)
{
    unsigned int addr_temp;
    unsigned int data;
    unsigned char phy_address, reg_address;

    addr_temp = (s27_addr) >> 2;
    data = addr_temp >> 7;

    phy_address = 0x1f;
    reg_address = 0x10;

    phy_reg_write(phy_address, reg_address, data);

    phy_address = (0x17 & ((addr_temp >> 4) | 0x10));

    reg_address = (((addr_temp << 1) & 0x1e) | 0x1);
    data = (s27_write_data >> 16) & 0xffff;
    phy_reg_write(phy_address, reg_address, data);

    reg_address = ((addr_temp << 1) & 0x1e);
    data = s27_write_data  & 0xffff;
    phy_reg_write(phy_address, reg_address, data);
}

static unsigned int __init athrs27_reg_read(unsigned int s27_addr)
{
    unsigned int addr_temp;
    unsigned int s27_rd_csr_low, s27_rd_csr_high, s27_rd_csr;
    unsigned int data;
    unsigned char phy_address, reg_address;

    addr_temp = s27_addr >>2;
    data = addr_temp >> 7;

    phy_address = 0x1f;
    reg_address = 0x10;

    phy_reg_write(phy_address, reg_address, data);

    phy_address = (0x17 & ((addr_temp >> 4) | 0x10));
    reg_address = ((addr_temp << 1) & 0x1e);
    s27_rd_csr_low = (unsigned int) phy_reg_read(phy_address, reg_address);

    reg_address = reg_address | 0x1;
    s27_rd_csr_high = (unsigned int) phy_reg_read(phy_address, reg_address);
    s27_rd_csr = (s27_rd_csr_high << 16) | s27_rd_csr_low ;

    return (s27_rd_csr);
}

static void __init ar8236_reset(void)
{
    unsigned short i = 60;

    athrs27_reg_write(0x0, athrs27_reg_read(0x0) | 0x80000000);
    while (i--)
    {
        mdelay(100);
        if (!(athrs27_reg_read(0x0) & 0x80000000))
        break;
    }
}

static void __init tl_wr2041n_v2_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f01fc00);
	u8 *art = (u8 *) KSEG1ADDR(0x1fff1000);

	ath79_register_m25p80(&wr2041n_v2_flash_data);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(tl_wr2041_v2_leds_gpio),
				 tl_wr2041_v2_leds_gpio);
	ath79_register_gpio_keys_polled(-1, TL_WR2041_V2_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(tl_wr2041_v2_gpio_keys),
					tl_wr2041_v2_gpio_keys);

	ath79_register_wmac(art, mac);

	ar8236_reset();

	ath79_setup_qca955x_eth_cfg(QCA955X_ETH_CFG_GE0_MII_EN | 
                    QCA955X_ETH_CFG_GE0_MII_SLAVE);

	ath79_register_mdio(1, 0x0);
	ath79_register_mdio(0, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 1);

	mdiobus_register_board_info(wr2041n_v2_mdio0_info,
				    ARRAY_SIZE(wr2041n_v2_mdio0_info));

	/* GMAC0 is connected to an AR8236 switch */
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;

	ath79_register_eth(0);
}

MIPS_MACHINE(ATH79_MACH_TL_WR2041N_V2, "TL-WR2041N-v2",
	     "TP-LINK TL-WR2041N v2", tl_wr2041n_v2_setup);

