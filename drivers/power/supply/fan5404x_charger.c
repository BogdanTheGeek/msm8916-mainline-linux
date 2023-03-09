// SPDX-License-Identifier: GPL-2.0-only
/*
 * ON Semiconductor FAN5404x charger driver
 *
 * Copyright (C) 2023 Bogdan Ionescu <bogdan.ionescu.work+kernel@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/usb/phy.h>
#include <linux/power_supply.h>

enum fan5404x_regs {
	FAN5404X_REG_CTRL0 = 0x00,
	FAN5404X_REG_CTRL1 = 0x01,
	FAN5404X_REG_OREG = 0x02,
	FAN5404X_REG_IC_INFO = 0x03,
	FAN5404X_REG_IBAT = 0x04,
	FAN5404X_REG_VBUS_CTRL = 0x05,
	FAN5404X_REG_SAFETY = 0x06,
	FAN5404X_REG_POST_CHRG = 0x07,
	FAN5404X_REG_MON0 = 0x10,
	FAN5404X_REG_MON1 = 0x11,
	FAN5404X_REG_NTC = 0x12,
	FAN5404X_REG_WD_CTRL = 0x13,
	FAN5404X_REG_RESTART = 0xfa,
};

/* Register filed definitions */
/* Undefined fields shall be considered reserved */
enum fan5404x_ctrl0 {
	CTRL0_FIELD_FAULT = GENMASK(2, 0),
	CTRL0_FIELD_BOOST = BIT(3),
	CTRL0_FIELD_STAT = GENMASK(5, 4),
	CTRL0_FIELD_EN_STAT = BIT(6),
	CTRL0_FIELD_TMR_RST = BIT(7),
};

enum fan5404x_ctrl1 {
	CTRL1_FIELD_OPA_MODE = BIT(0),
	CTRL1_FIELD_HZ_MODE = BIT(1),
	CTRL1_FIELD_CE_N = BIT(2),
	CTRL1_FIELD_TE = BIT(3),
	CTRL1_FIELD_V_LOWV = GENMASK(5, 4),
	CTRL1_FIELD_I_BUSLIM = GENMASK(7, 6),
};

enum fan5404x_oreg {
	OREG_FIELD_EOC = BIT(0),
	OREG_FIELD_DBAT_B = BIT(1),
	OREG_FIELD_OREG = GENMASK(7, 2),
};

enum fan5404x_ic_info {
	INFO_FIELD_REV = GENMASK(2, 0),
	INFO_FIELD_PN = GENMASK(5, 3),
	INFO_FIELD_VENDOR_CODE = GENMASK(7, 6),
};

enum fan5404x_ibat {
	IBAT_FIELD_ITERM = GENMASK(2, 0),
	IBAT_FIELD_IOCHARGE = GENMASK(6, 3),
	IBAT_FIELD_RESET = BIT(7),
};

enum fan5404x_vbus_ctrl {
	VBUS_CTRL_FIELD_VBUSLIM = GENMASK(2, 0),
	VBUS_CTRL_FIELD_SP = BIT(3),
	VBUS_CTRL_FIELD_VBUS_CON = BIT(4),
	VBUS_CTRL_FIELD_IO_LEVEL = BIT(5),
	VBUS_CTRL_FIELD_PROD = BIT(6),
};

enum fan5404x_safety {
	SAFETY_FIELD_VSAFE = GENMASK(3, 0),
	SAFETY_FIELD_ISAFE = GENMASK(7, 4),
};

enum fan5404x_post_chrg {
	POST_CHRG_FIELD_PC_IT = GENMASK(2 : 0),
	POST_CHRG_FIELD_PC_EN = BIT(3),
	POST_CHRG_FIELD_VBUS_LOAD = GENMASK(5, 4),
	POST_CHRG_FIELD_BDET = GENMASK(7, 6),
};

enum fan5404x_mon0 {
	MON0_FIELD_CV = BIT(0),
	MON0_FIELD_VBUS_VALID = BIT(1),
	MON0_FIELD_IBUS = BIT(2),
	MON0_FIELD_ICHG = BIT(3),
	MON0_FIELD_T_120 = BIT(4),
	MON0_FIELD_LINCHG = BIT(5),
	MON0_FIELD_VBAT_CMP = BIT(6),
	MON0_FIELD_ITERM_CMP = BIT(7),
};

enum fan5404x_mon1 {
	MON1_FIELD_PC_ON = BIT(2),
	MON1_FIELD_NOBAT = BIT(3),
	MON1_FIELD_DIS_LEVEL = BIT(4),
	MON1_FIELD_POK_B = BIT(5),
	MON1_FIELD_VBAT = BIT(6),
	MON1_FIELD_GATE = BIT(7),
};

enum fan5404x_ntc {
	NTC_FIELD_TH = GENMASK(3, 0),
	NTC_FIELD_OK = BIT(4),
	NTC_FIELD_TEMP_DIS = BIT(5),
};

enum fan5404x_wd_ctrl {
	WD_CTRL_FIELD_WD_DIS = BIT(1),
	WD_CTRL_FIELD_EN_VREG = BIT(2),
};

enum fan5404x_restart {
	RESTART_FIELD = GENMASK(7, 0),
};

/* Field value definitions */
enum fan5404x_status_value {
	STATUS_READY = 0,
	STATUS_PWM_EN = 1,
	STATUS_CHARGE_DONE = 2,
	STATUS_FAULT = 3,
};

enum fan5404x_fault_value {
	FAULT_NONE = 0,
	FAULT_VBUS_OVP = 1,
	FAULT_SLEEP_MODE = 2,
	FAULT_POOR_INPUT_SOURCE = 3,
	FAULT_BATTERY_OVP = 4,
	FAULT_THERMAL_SHUTDOW = 5,
	FAULT_TIMER_FAULT = 6,
	FAULT_NO_BATTERY = 7,
};

enum fan5404x_bdet_value {
	BDET_ALWAYS = 0,
	BDET_DISABLE_NORMAL = 1,
	BDET_DISABLE_AFTER_RESTART = 2,
	BDET_DISABLE_NTC_FAULT = 3,
};

enum fan5404x_vbus_load_value {
	VBUS_LOAD_NONE = 0,
	VBUS_LOAD_4_MS = 1,
	VBUS_LOAD_131_MS = 2,
	VBUS_LOAD_135_MS = 3,
};

enum fan5404x_restart_value {
	RESTART = 0xb5,
}

static uint8_t fan5404x_get_iocharge(const uint32_t current_ma)
{
	if (current_ma < 550)
		return 0;
	else
		return ((current_ma - 550) / 100);
}

static uint8_t fan5404x_get_iterm(const uint32_t current_ma)
{
	if (current_ma < 50)
		return 0;
	else
		return ((current_ma - 50) / 50);
}

static uint8_t fan5404x_get_vsafe(const uint32_t voltage_mv)
{
	if (voltage_mv < 4200)
		return 0;
	else
		return ((voltage_mv - 4200) / 20);
}

static uint8_t fan5404x_get_isafe(const uint32_t current_ma)
{
	return fan5404x_get_iocharge(current_ma);
}

static uint8_t fan5404x_get_vbus_limit(const uint32_t voltage_mv)
{
	if (voltage_mv < 4213)
		return 0;
	else if (voltage_mv > 4773)
		return 7;
	else
		return ((voltage_mv - 4213) / 20);
}

static uint8_t fan5404x_get_ibus_limit(const uint32_t current_ma)
{
	/* Assuming that we always want to limit the current under the requested
    * value. The only possible values are 100mA, 500mA, 800mA and no limit.
    */
	if (current_ma < 500)
		return 0;
	else if (current_ma < 800)
		return 1;
	else if (current_ma == 800)
		return 2;
	else if (current_ma > 800)
		return 3;
}

static uint8_t fan5404x_get_oreg(const uint32_t voltage_mv)
{
	if (voltage_mv < 3500)
		return 0;
	else
		return ((voltage_mv - 3500) / 20);
}

static int fan5404x_probe(struct i2c_client *client)
{
}

static const struct i2c_device_id fan5404x_id[] = {
	{ "fan5404x", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, fan5404x_id);

static const struct of_device_id of_fan5404x_leds_match[] = {
	{ .compatible = "on,fan5404x" },
	{},
};
MODULE_DEVICE_TABLE(of, of_fan5404x_leds_match);

static struct i2c_driver fan5404x_i2c_driver = {
	.driver = {
		.name = "fan5404x",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_fan5404x_leds_match),
		  },
	.probe_new = fan5404x_probe,
	.id_table = fan5404x_id,
};

module_i2c_driver(fan5404x_i2c_driver);

MODULE_AUTHOR("Bogdan Ionescu <bogdan.ionescu.work+kernel@gmail.com>");
MODULE_DESCRIPTION("ON Semiconductor FAN5404x charger dirver");
MODULE_LICENSE("GPL");
