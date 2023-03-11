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
	FAN5404X_REG_MAX = FAN5404X_REG_RESTART,
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
	POST_CHRG_FIELD_PC_IT = GENMASK(2, 0),
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
};

struct fan5404x {
	struct i2c_client *client;
	struct regmap *regmap;
};

static enum power_supply_property fan5404x_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH, POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	/*
	POWER_SUPPLY_PROP_AUTHENTIC,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
   */
};

static const struct regmap_config fan5404x_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FAN5404X_REG_MAX,
};

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

static int fan5404x_get_status(const struct fan5404x *const charger)
{
	struct device *dev = &charger->client->dev;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	int err;
	int ctrl0, ctrl1;

	err = regmap_read(charger->regmap, FAN5404X_REG_CTRL0, &ctrl0);
	err |= regmap_read(charger->regmap, FAN5404X_REG_CTRL1, &ctrl1);

	if (!err) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		if ((ctrl0 & CTRL0_FIELD_STAT) == STATUS_CHARGE_DONE << 4)
			status = POWER_SUPPLY_STATUS_FULL;
		if ((ctrl0 & CTRL0_FIELD_STAT) == STATUS_PWM_EN << 4)
			status = (ctrl1 & CTRL1_FIELD_CE_N) ?
					 POWER_SUPPLY_STATUS_NOT_CHARGING :
					 POWER_SUPPLY_STATUS_CHARGING;
	} else {
		dev_err(dev, "failed get status");
	}

	return status;
}

static int fan5404x_prop_writeable(struct power_supply *psy,
				   enum power_supply_property psp)
{
	struct fan5404x *charger = power_supply_get_drvdata(psy);
	struct device *dev = &charger->client->dev;

	dev_info(dev, "is writable: %d", psp);

	switch (psp) {
	default:
		return 0;
	}
}

static int fan5404x_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct fan5404x *charger = power_supply_get_drvdata(psy);
	struct device *dev = &charger->client->dev;

	dev_info(dev, "get_property: %d", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = fan5404x_get_status(charger);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int fan5404x_set_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 const union power_supply_propval *val)
{
	struct fan5404x *charger = power_supply_get_drvdata(psy);
	struct device *dev = &charger->client->dev;

	dev_info(dev, "set_property: %d", psp);

	switch (psp) {
	default:
		return -EINVAL;
	}
}

static const struct power_supply_desc fan5404x_psy_desc = {
	.name = "fan5404x",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = fan5404x_psy_props,
	.num_properties = ARRAY_SIZE(fan5404x_psy_props),
	.get_property = fan5404x_get_property,
	.set_property = fan5404x_set_property,
	.property_is_writeable = fan5404x_prop_writeable,
};

static void fan5404x_task(struct work_struct *work)
{
	/* Every 32s we need to to reset the T32 timer to continue charging. */
}

static int fan5404x_probe(struct i2c_client *client)
{
	int ret;
	struct device *dev = &client->dev;
	struct power_supply_config psy_cfg = {};
	struct power_supply *psy;
	struct fan5404x *charger;

	charger = devm_kzalloc(dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	i2c_set_clientdata(client, charger);
	charger->client = client;

	charger->regmap = devm_regmap_init_i2c(client, &fan5404x_regmap_conf);
	if (IS_ERR(charger->regmap))
		return dev_err_probe(dev, PTR_ERR(charger->regmap),
				     "cannot allocate regmap\n");

	psy_cfg.drv_data = charger;
	psy_cfg.of_node = dev->of_node;
   /*
	psy_cfg.supplied_to = supplied_to;
	psy_cfg.num_supplicants = num_supplicants;
   */

	psy = devm_power_supply_register_no_ws(dev, &fan5404x_psy_desc,
					       &psy_cfg);
	if (IS_ERR(psy))
		return dev_err_probe(dev, PTR_ERR(psy), "cannot register\n");

	dev_info(dev, "Driver intialised");

	return 0;
}

static const struct i2c_device_id fan5404x_id[] = {
	{ "fan54041", 0 },
	{ "fan54042", 0 },
	{ "fan54043", 0 },
	{ "fan54045", 0 },
	{ "fan54046", 0 },
	{ "fan54047", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, fan5404x_id);

static const struct of_device_id of_fan5404x_leds_match[] = {
	{ .compatible = "onsemi,fan54040" },
	{ .compatible = "onsemi,fan54041" },
	{ .compatible = "onsemi,fan54042" },
	{ .compatible = "onsemi,fan54045" },
	{ .compatible = "onsemi,fan54046" },
	{ .compatible = "onsemi,fan54047" },
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
