/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 * Copyright (c) 2018 Razer Inc.
 * Copyright (c) 2018 Paranoid Android.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/log2.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include "smb-reg.h"
#include "smb-lib.h"
#include "storm-watch.h"
#include <linux/pmic-voter.h>

#define SMB2_DEFAULT_WPWR_UW	8000000

static struct smb_params v1_params = {
	.fcc			= {
		.name	= "fast charge current",
		.reg	= FAST_CHARGE_CURRENT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4500000,
		.step_u	= 25000,
	},
	.fv			= {
		.name	= "float voltage",
		.reg	= FLOAT_VOLTAGE_CFG_REG,
		.min_u	= 3487500,
		.max_u	= 4920000,
		.step_u	= 7500,
	},
	.usb_icl		= {
		.name	= "usb input current limit",
		.reg	= USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.icl_stat		= {
		.name	= "input current limit status",
		.reg	= ICL_STATUS_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 250000,
		.max_u	= 2000000,
		.step_u	= 250000,
	},
	.dc_icl			= {
		.name	= "dc input current limit",
		.reg	= DCIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 6000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_lv		= {
		.name	= "dc icl PT <8V",
		.reg	= ZIN_ICL_PT_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_hv		= {
		.name	= "dc icl PT >8V",
		.reg	= ZIN_ICL_PT_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_lv		= {
		.name	= "dc icl div2 <5.5V",
		.reg	= ZIN_ICL_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_lv	= {
		.name	= "dc icl div2 5.5-6.5V",
		.reg	= ZIN_ICL_MID_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_hv	= {
		.name	= "dc icl div2 6.5-8.0V",
		.reg	= ZIN_ICL_MID_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_hv		= {
		.name	= "dc icl div2 >8.0V",
		.reg	= ZIN_ICL_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.jeita_cc_comp		= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	/* WayneWCShiue - 9801-468 - [BAT] Jeita temperature protection */
	.jeita_fv_comp		= {
		.name	= "jeita fv reduction",
		.reg	= JEITA_FVCOMP_CFG_REG,
		.min_u	= 0,
		.max_u	= 475000,
		.step_u	= 7500,
	},
	/* end 9801-468 */
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BUCK_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BOOST_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
};

static struct smb_params pm660_params = {
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
};

struct smb_dt_props {
	int	usb_icl_ua;
	int	dc_icl_ua;
	int	boost_threshold_ua;
	int	wipower_max_uw;
	int	min_freq_khz;
	int	max_freq_khz;
	/* WayneWCShiue - 9801-468 - [BAT] Jeita temperature protection */
	int	jeita_fcc_comp;
	int	jeita_fv_comp;
	/* end 9801-468 */
	struct	device_node *revid_dev_node;
	int	float_option;
	int	chg_inhibit_thr_mv;
	bool	no_battery;
	bool	hvdcp_disable;
	bool	auto_recharge_soc;
	int	wd_bark_time;
};

struct smb2 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
	bool			bad_part;
};

static int __debug_mask;
module_param_named(
	debug_mask, __debug_mask, int, S_IRUSR | S_IWUSR
);

static int __weak_chg_icl_ua = 500000;
module_param_named(
	weak_chg_icl_ua, __weak_chg_icl_ua, int, S_IRUSR | S_IWUSR);

static int __try_sink_enabled = 1;
module_param_named(
	try_sink_enabled, __try_sink_enabled, int, 0600
);

#define MICRO_1P5A		1500000
#define MICRO_P1A		100000
#define OTG_DEFAULT_DEGLITCH_TIME_MS	50
#define MIN_WD_BARK_TIME		16
#define DEFAULT_WD_BARK_TIME		64
#define BITE_WDOG_TIMEOUT_8S		0x3
#define BARK_WDOG_TIMEOUT_MASK		GENMASK(3, 2)
#define BARK_WDOG_TIMEOUT_SHIFT		2
/* FIH - akckwang - 9801-506 - Create a node to on/off otg */
#if defined(CONFIG_FIH_9801) || defined(CONFIG_FIH_9802)
static struct smb2 *mChip = NULL;
char fih_otg_disable_mode = 0; // FIHTDC, IdaChiang, add for OTG FREQ
static ssize_t fih_otg_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	pr_info("fih_otg_show = %d\n", fih_otg_disable_mode);
	return sprintf(buf, "%d\n", fih_otg_disable_mode);
}

static ssize_t fih_otg_store(struct device *dev,
		struct device_attribute *attr, const char
		*buf, size_t size)
{
	struct smb_charger *chg = &mChip->chg;
	int intval =0;
	int rc;
	u8 stat;
	int reg_enabled = -1;

	sscanf(buf, "%d", &intval);

	if(intval !=0 && intval !=1){
		pr_info("%s:Invalid argument:%s\n", __func__, buf);
		return -EINVAL;
	}
	fih_otg_disable_mode = intval;

	reg_enabled = smblib_vbus_regulator_is_enabled(chg->vbus_vreg->rdev);
	if(reg_enabled != 0 && reg_enabled != 1){
		pr_err("%s:Can't get regulator status:%d\n", __func__, reg_enabled);
		goto end;
	}

	if(intval == 1){
		if(reg_enabled){
			pr_info("%s:Disable otg\n", __func__);
			smblib_vbus_regulator_disable(chg->vbus_vreg->rdev);
		}
		else{
			pr_info("%s:Disable otg, but otg isn't enabled\n", __func__);
		}
	}
	else{
		rc = smblib_read(chg, TYPE_C_STATUS_2_REG, &stat);
		if(rc < 0){
			pr_err("%s:Couldn't read TYPE_C_STATUS_2 rc=%d\n", __func__, rc);
			stat = 0;
		}

		if((stat & DFP_TYPEC_MASK) == DFP_RD_OPEN_BIT &&
			(stat & EXIT_UFP_MODE_BIT) &&
			!reg_enabled)
		{
			pr_info("%s:Enable otg\n", __func__);
			smblib_vbus_regulator_enable(chg->vbus_vreg->rdev);
		}
		else{
			pr_info("%s:Enable otg, but otg is not attached\n", __func__);
		}
	}

end:
	return size;
}

static DEVICE_ATTR(fih_otg_fun, 0644, fih_otg_show, fih_otg_store);
#endif
/* end FIH - 9801-506 */

static void razer_charge_limit_disable_charge(struct smb_charger *chg, bool disable) {
	int rc;

	/* If current vote status equals the request, we don't need to do anything */
	if (disable == chg->razer_charge_limit_active)
		return;

	/* Place or remove vote for usb icl */
	rc = vote(chg->usb_icl_votable, RAZER_LIMIT_VOTER, disable, 0);
	if (rc < 0)
		goto error;

	/* Place or remove vote for dc suspend */
	rc = vote(chg->dc_suspend_votable, RAZER_LIMIT_VOTER, disable, 0);
	if (rc < 0)
		goto error;

	/* Place or remove vote for sending a hardware signal to disable charging */
	rc = vote(chg->chg_disable_votable, RAZER_LIMIT_VOTER, disable, 0);
	if (rc < 0)
		goto error;

	/* If everything went fine, update the variable */
	chg->razer_charge_limit_active = disable;

	/* Notify batt_psy about this change */
	power_supply_changed(chg->batt_psy);

	pr_info("%s: Successfully %s charging.",
		__func__, disable ? "disabled" : "enabled");

	/* Return to avoid error path */
	return;

error:
	pr_err("%s: Vote to %s charging as part of charge limit failed! rc=%d\n",
		__func__, disable ? "disable" : "enable", rc);
}

void razer_charge_limit_update(struct smb_charger *chg) {
	union power_supply_propval cur_pct = {0, };
	int rc;

	mutex_lock(&chg->razer_charge_limit_lock);

	/* Remove votes if disabled or not connected */
	if (!chg->razer_charge_limit_enable || chg->typec_mode == POWER_SUPPLY_TYPEC_NONE) {
		razer_charge_limit_disable_charge(chg, false);
		goto end;
	}

	/* Dropdown capacity cannot be higher than or identical to max limit capacity */
	if (chg->razer_charge_limit_dropdown >= chg->razer_charge_limit_max) {
		pr_err("%s: Charging limit configuration is not sane, disabling it.", __func__);
		chg->razer_charge_limit_enable = false;
		razer_charge_limit_disable_charge(chg, false);
		goto end;
	}

	rc = smblib_get_prop_batt_capacity(chg, &cur_pct);
	if (rc < 0) {
		pr_err("%s: Failed to get battery capacity, aborting. rc=%d", __func__, rc);
		goto end;
	}

	if (cur_pct.intval >= chg->razer_charge_limit_max) {
		/* Disable charging if the current capacity is at max limit or higher */
		razer_charge_limit_disable_charge(chg, true);
	} else if (cur_pct.intval <= chg->razer_charge_limit_dropdown) {
		/* Enable charging if we are at dropdown capacity or lower */
		razer_charge_limit_disable_charge(chg, false);
	}

end:
	mutex_unlock(&chg->razer_charge_limit_lock);
}

static ssize_t razer_charge_limit_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct smb_charger *chg = &mChip->chg;

	return snprintf(buf, PAGE_SIZE, "%d\n", chg->razer_charge_limit_enable);
}

static ssize_t razer_charge_limit_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t size)
{
	struct smb_charger *chg = &mChip->chg;
	int intval;

	if (sscanf(buf, "%d", &intval) < 1) {
		pr_err("%s: sscanf failed to parse buffer.\n", __func__);
		return -EINVAL;
	}

	if (intval != 0 && intval != 1) {
		pr_err("%s: Invalid argument: %s\n", __func__, buf);
		return -EINVAL;
	}
	chg->razer_charge_limit_enable = intval;

	/* Update charge limit state for the new data */
	razer_charge_limit_update(chg);

	return size;
}
static DEVICE_ATTR(razer_charge_limit_enable, S_IRUGO | S_IWUSR,
	razer_charge_limit_enable_show, razer_charge_limit_enable_store);

static ssize_t razer_charge_limit_max_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct smb_charger *chg = &mChip->chg;

	return snprintf(buf, PAGE_SIZE, "%d\n", chg->razer_charge_limit_max);
}

#define RAZER_CHARGE_LIMIT_MAX_UPPER 99
#define RAZER_CHARGE_LIMIT_MAX_LOWER 2
static ssize_t razer_charge_limit_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t size)
{
	struct smb_charger *chg = &mChip->chg;
	int intval;

	if (sscanf(buf, "%d", &intval) < 1) {
		pr_err("%s: sscanf failed to parse buffer.\n", __func__);
		return -EINVAL;
	}

	if (intval < RAZER_CHARGE_LIMIT_MAX_LOWER || intval > RAZER_CHARGE_LIMIT_MAX_UPPER) {
		pr_err("%s: Invalid argument: %s\n", __func__, buf);
		return -EINVAL;
	}
	chg->razer_charge_limit_max = intval;

	/* Update charge limit state for the new data */
	razer_charge_limit_update(chg);

	return size;
}
static DEVICE_ATTR(razer_charge_limit_max, S_IRUGO | S_IWUSR,
	razer_charge_limit_max_show, razer_charge_limit_max_store);

static ssize_t razer_charge_limit_dropdown_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct smb_charger *chg = &mChip->chg;

	return snprintf(buf, PAGE_SIZE, "%d\n", chg->razer_charge_limit_dropdown);
}

#define RAZER_CHARGE_LIMIT_DROPDOWN_UPPER 98
#define RAZER_CHARGE_LIMIT_DROPDOWN_LOWER 1
static ssize_t razer_charge_limit_dropdown_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t size)
{
	struct smb_charger *chg = &mChip->chg;
	int intval;

	if (sscanf(buf, "%d", &intval) < 1) {
		pr_err("%s: sscanf failed to parse buffer.\n", __func__);
		return -EINVAL;
	}

	if (intval < RAZER_CHARGE_LIMIT_DROPDOWN_LOWER || intval > RAZER_CHARGE_LIMIT_DROPDOWN_UPPER) {
		pr_err("%s: Invalid argument: %s\n", __func__, buf);
		return -EINVAL;
	}
	chg->razer_charge_limit_dropdown = intval;

	/* Update charge limit state for the new data */
	razer_charge_limit_update(chg);

	return size;
}
static DEVICE_ATTR(razer_charge_limit_dropdown, S_IRUGO | S_IWUSR,
	razer_charge_limit_dropdown_show, razer_charge_limit_dropdown_store);

/* WayneWCShiue - 9801-6414 - Add battery event for problem report */
static bool charging_current_too_low(struct smb_charger * chg, int cmp_current_ma)
{
	union power_supply_propval pval = {0, };
	u8 stat = 0;
	int rc;

	smblib_get_prop_batt_status(chg, &pval);
	if(pval.intval != POWER_SUPPLY_STATUS_CHARGING)
		return false;

	rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
	if(rc < 0 || (stat & (QC_2P0_BIT | QC_3P0_BIT)) == 0)
		return false;

	rc = smblib_get_prop_batt_capacity(chg, &pval);
	if(rc < 0 || pval.intval >= 80)
		return false;

	rc = smblib_get_prop_system_temp_level(chg, &pval);
	if(rc < 0 || pval.intval != 0)
		return false;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat);
	if(rc < 0 || (stat & (BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT | BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT
		| BAT_TEMP_STATUS_TOO_HOT_BIT | BAT_TEMP_STATUS_TOO_COLD_BIT)))
		return false;

	rc = smblib_get_prop_from_bms(chg, POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
	cmp_current_ma = (-1) * cmp_current_ma * 1000;
	if(rc < 0 || pval.intval < cmp_current_ma)	// the charging current should be more than 1000 mA with QC2 or QC3 charger
		return false;

	return true;
}

static int collect_monitor_event(struct smb_charger * chg)
{
	union power_supply_propval pval = {0, };
	int monitor_event = 0;

	smblib_get_prop_batt_health(chg, &pval);

	if (pval.intval == POWER_SUPPLY_HEALTH_COLD)
		monitor_event |= 0x01;
	else if (pval.intval == POWER_SUPPLY_HEALTH_OVERHEAT)
		monitor_event |= 0x02;
	else if (pval.intval == POWER_SUPPLY_HEALTH_OVERVOLTAGE)
		monitor_event |= 0x04;

	if(charging_current_too_low(chg, 1000))
		return monitor_event |= 0x08;
	else
		return monitor_event;
}
/* end 9801-6414 */

static int smb2_parse_dt(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	int rc, byte_len;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chg->step_chg_enabled = of_property_read_bool(node,
				"qcom,step-charging-enable");

	chg->sw_jeita_enabled = of_property_read_bool(node,
				"qcom,sw-jeita-enable");

	rc = of_property_read_u32(node, "qcom,wd-bark-time-secs",
					&chip->dt.wd_bark_time);
	if (rc < 0 || chip->dt.wd_bark_time < MIN_WD_BARK_TIME)
		chip->dt.wd_bark_time = DEFAULT_WD_BARK_TIME;

	chip->dt.no_battery = of_property_read_bool(node,
						"qcom,batteryless-platform");

	rc = of_property_read_u32(node,
				"qcom,fcc-max-ua", &chg->batt_profile_fcc_ua);
	if (rc < 0)
		chg->batt_profile_fcc_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,fv-max-uv", &chg->batt_profile_fv_uv);
	if (rc < 0)
		chg->batt_profile_fv_uv = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
	if (rc < 0)
		chip->dt.usb_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,otg-cl-ua", &chg->otg_cl_ua);
	if (rc < 0)
		chg->otg_cl_ua = MICRO_1P5A;

	rc = of_property_read_u32(node,
				"qcom,dc-icl-ua", &chip->dt.dc_icl_ua);
	if (rc < 0)
		chip->dt.dc_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,boost-threshold-ua",
				&chip->dt.boost_threshold_ua);
	if (rc < 0)
		chip->dt.boost_threshold_ua = MICRO_P1A;

	rc = of_property_read_u32(node,
				"qcom,min-freq-khz",
				&chip->dt.min_freq_khz);
	if (rc < 0)
		chip->dt.min_freq_khz = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,max-freq-khz",
				&chip->dt.max_freq_khz);
	if (rc < 0)
		chip->dt.max_freq_khz = -EINVAL;

	rc = of_property_read_u32(node, "qcom,wipower-max-uw",
				&chip->dt.wipower_max_uw);
	if (rc < 0)
		chip->dt.wipower_max_uw = -EINVAL;

	if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
		chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
			GFP_KERNEL);

		if (chg->thermal_mitigation == NULL)
			return -ENOMEM;

		chg->thermal_levels = byte_len / sizeof(u32);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chg->thermal_mitigation,
				chg->thermal_levels);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read threm limits rc = %d\n", rc);
			return rc;
		}
	}

	of_property_read_u32(node, "qcom,float-option", &chip->dt.float_option);
	if (chip->dt.float_option < 0 || chip->dt.float_option > 4) {
		pr_err("qcom,float-option is out of range [0, 4]\n");
		return -EINVAL;
	}

	chip->dt.hvdcp_disable = of_property_read_bool(node,
						"qcom,hvdcp-disable");

	of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
				&chip->dt.chg_inhibit_thr_mv);
	if ((chip->dt.chg_inhibit_thr_mv < 0 ||
		chip->dt.chg_inhibit_thr_mv > 300)) {
		pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
		return -EINVAL;
	}

	chip->dt.auto_recharge_soc = of_property_read_bool(node,
						"qcom,auto-recharge-soc");

	chg->micro_usb_mode = of_property_read_bool(node, "qcom,micro-usb");

	chg->dcp_icl_ua = chip->dt.usb_icl_ua;

	chg->suspend_input_on_debug_batt = of_property_read_bool(node,
					"qcom,suspend-input-on-debug-batt");

	rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
					&chg->otg_delay_ms);
	if (rc < 0)
		chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;

	chg->fcc_stepper_mode = of_property_read_bool(node,
					"qcom,fcc-stepping-enable");
	/* WayneWCShiue - 9801-468 - [BAT] Jeita temperature protection */
	rc = of_property_read_u32(node, "qcom,jeita-fcc-comp",
			&chip->dt.jeita_fcc_comp);
	if (rc < 0) {
		pr_err("fg-fcc-comp unavailable, rc:%d\n", rc);
		chip->dt.jeita_fcc_comp = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,jeita-fv-comp",
			&chip->dt.jeita_fv_comp);
	if (rc < 0) {
		pr_err("fg-fcc-comp unavailable, rc:%d\n", rc);
		chip->dt.jeita_fv_comp = -EINVAL;
	}
	/* end 9801-468 */

	/* WayneWCShiue - 9802-799 - Implement the WLC FCC adjust mechansim */
	chg->fih_wlc_fcc_en = of_property_read_bool(node, "fih,wlc-fcc-en");
	/* end 9802-799 */

	/* WayneWCShiue - 9801-1665 - Add FIH abnormal charging current check mechanism */
	chg->fih_chg_abnormal_check_en = of_property_read_bool(node, "fih,chg-abnormal-check-en");
	if(chg->fih_chg_abnormal_check_en == true) {
		chg->fih_reEnable_max_limit = 0;
	}
	/* end 9801-1665 */

	/* WayneWCShiue - 9801-3293 - Show battery info */
	chg->show_batt_info_en = of_property_read_bool(node, "fih,show-batt-info-en");
	/* end 9801-3293 */

	return 0;
}

/************************
 * USB PSY REGISTRATION *
 ************************/

static enum power_supply_property smb2_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
	POWER_SUPPLY_PROP_PD_ALLOWED,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_PR_SWAP,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_SDP_CURRENT_MAX,
};

static int smb2_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (chip->bad_part)
			val->intval = 1;
		else
			rc = smblib_get_prop_usb_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_usb_online(chg, val);
		if (!val->intval)
			break;

		if ((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT ||
			chg->micro_usb_mode) &&
			chg->real_charger_type == POWER_SUPPLY_TYPE_USB)
			val->intval = 0;
		else
			val->intval = 1;
		//{Fix the DUT showed "Charging on AC" and then showed "Charging over USB
		if (chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
			val->intval = 0;
		//}Fix the DUT showed "Charging on AC" and then showed "Charging over USB
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_usb_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_usb_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, PD_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		//{Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
		//Qcom resolve way different with us.
		//rc = smblib_get_prop_input_current_settled(chg, val);
		rc = smblib_get_charge_current(chg, &val->intval);
		//}Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPE_USB_PD;
		else
			val->intval = chg->real_charger_type;
		break;
	case POWER_SUPPLY_PROP_TYPEC_MODE:
/* FIH - akckwang - 9801-680 - Dump typec sts register value */
#if 0
		smblib_dump_typec_sts(chg, val);
#endif
/* end FIH - 9801-680 */
		if (chg->micro_usb_mode)
			val->intval = POWER_SUPPLY_TYPEC_NONE;
		else if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPEC_SOURCE_DEFAULT;
		else
			val->intval = chg->typec_mode;
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		if (chg->micro_usb_mode)
			val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		else
			rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		if (chg->micro_usb_mode)
			val->intval = 0;
		else
			rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ALLOWED:
		rc = smblib_get_prop_pd_allowed(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		val->intval = chg->pd_active;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		rc = smblib_get_prop_usb_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		val->intval = chg->boost_current_ua;
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		val->intval = chg->system_suspend_supported;
		break;
	case POWER_SUPPLY_PROP_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_get_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		val->intval = chg->voltage_min_uv;
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable,
					      USB_PSY_VOTER);
		break;
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	mutex_lock(&chg->lock);
	if (!chg->typec_present) {
		rc = -EINVAL;
		goto unlock;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		chg->system_suspend_supported = val->intval;
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		rc = smblib_set_prop_boost_current(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val->intval >= 0, val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_set_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		rc = smblib_set_prop_pd_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		rc = smblib_set_prop_pd_voltage_min(chg, val);
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		rc = smblib_set_prop_sdp_current_max(chg, val);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

unlock:
	mutex_unlock(&chg->lock);
	return rc;
}

static int smb2_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
//DY-FixPostCSBuildFail        case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int smb2_init_usb_psy(struct smb2 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	chg->usb_psy_desc.name			= "usb";
	chg->usb_psy_desc.type			= POWER_SUPPLY_TYPE_USB_PD;
	chg->usb_psy_desc.properties		= smb2_usb_props;
	chg->usb_psy_desc.num_properties	= ARRAY_SIZE(smb2_usb_props);
	chg->usb_psy_desc.get_property		= smb2_usb_get_prop;
	chg->usb_psy_desc.set_property		= smb2_usb_set_prop;
	chg->usb_psy_desc.property_is_writeable	= smb2_usb_prop_is_writeable;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = power_supply_register(chg->dev,
						  &chg->usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}

/********************************
 * USB PC_PORT PSY REGISTRATION *
 ********************************/
static enum power_supply_property smb2_usb_port_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	//{Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	//}Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
};

static int smb2_usb_port_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_usb_online(chg, val);
		if (!val->intval)
			break;

		if ((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT ||
			chg->micro_usb_mode) &&
			chg->real_charger_type == POWER_SUPPLY_TYPE_USB)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	//{Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
	//Qcom resolve way different with us, TBD.
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		//val->intval = 5000000;
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		//rc = smblib_get_prop_input_current_settled(chg, val);
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	//}Lock screen does not indicate charging speed, it shows "Charging" instead of "Charging slowly" or "Charging quickly" in all cases. This is not the expected behaviour.
	default:
		pr_err_ratelimited("Get prop %d is not supported in pc_port\n",
				psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb2_usb_port_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	default:
		pr_err_ratelimited("Set prop %d is not supported in pc_port\n",
				psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_port_psy_desc = {
	.name		= "pc_port",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= smb2_usb_port_props,
	.num_properties	= ARRAY_SIZE(smb2_usb_port_props),
	.get_property	= smb2_usb_port_get_prop,
	.set_property	= smb2_usb_port_set_prop,
};

static int smb2_init_usb_port_psy(struct smb2 *chip)
{
	struct power_supply_config usb_port_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_port_cfg.drv_data = chip;
	usb_port_cfg.of_node = chg->dev->of_node;
	chg->usb_port_psy = power_supply_register(chg->dev,
						  &usb_port_psy_desc,
						  &usb_port_cfg);
	if (IS_ERR(chg->usb_port_psy)) {
		pr_err("Couldn't register USB pc_port power supply\n");
		return PTR_ERR(chg->usb_port_psy);
	}

	return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb2_usb_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	/*
	 * TODO move the TEMP and TEMP_MAX properties here,
	 * and update the thermal balancer to look here
	 */
};

static int smb2_usb_main_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_MAIN;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, &val->intval);
		break;
	default:
		pr_debug("get prop %d is not supported in usb-main\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_main_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fcc, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_icl_current(chg, val->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
	.name		= "main",
	.type		= POWER_SUPPLY_TYPE_MAIN,
	.properties	= smb2_usb_main_props,
	.num_properties	= ARRAY_SIZE(smb2_usb_main_props),
	.get_property	= smb2_usb_main_get_prop,
	.set_property	= smb2_usb_main_set_prop,
};

static int smb2_init_usb_main_psy(struct smb2 *chip)
{
	struct power_supply_config usb_main_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_main_cfg.drv_data = chip;
	usb_main_cfg.of_node = chg->dev->of_node;
	chg->usb_main_psy = power_supply_register(chg->dev,
						  &usb_main_psy_desc,
						  &usb_main_cfg);
	if (IS_ERR(chg->usb_main_psy)) {
		pr_err("Couldn't register USB main power supply\n");
		return PTR_ERR(chg->usb_main_psy);
	}

	return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/

static enum power_supply_property smb2_dc_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
};

static int smb2_dc_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = get_effective_result(chg->dc_suspend_votable);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_dc_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_dc_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = POWER_SUPPLY_TYPE_WIPOWER;
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_dc_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = vote(chg->dc_suspend_votable, WBC_VOTER,
				(bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_dc_current_max(chg, val);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int smb2_dc_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc dc_psy_desc = {
	.name = "dc",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = smb2_dc_props,
	.num_properties = ARRAY_SIZE(smb2_dc_props),
	.get_property = smb2_dc_get_prop,
	.set_property = smb2_dc_set_prop,
	.property_is_writeable = smb2_dc_prop_is_writeable,
};

static int smb2_init_dc_psy(struct smb2 *chip)
{
	struct power_supply_config dc_cfg = {};
	struct smb_charger *chg = &chip->chg;

	dc_cfg.drv_data = chip;
	dc_cfg.of_node = chg->dev->of_node;
	chg->dc_psy = power_supply_register(chg->dev,
						  &dc_psy_desc,
						  &dc_cfg);
	if (IS_ERR(chg->dc_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->dc_psy);
	}

	return 0;
}

/*************************
 * BATT PSY REGISTRATION *
 *************************/

static enum power_supply_property smb2_batt_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_STATUS_INTERNAL, //Device was discharging when battery capacity is above 60% and temperature is warm.
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_DP_DM,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	/* WayneWCShiue - 9802-799 - Implement the WLC FCC adjust mechansim */
	POWER_SUPPLY_PROP_FIH_WLC_FCC_EN,
	/* end 9802-799 */
	/* WayneWCShiue - 9801-8555 - [BAT] Inform Battery Protect AP once the battery can only charge to 4.1V */
	POWER_SUPPLY_PROP_JEITA_FULL_CAPACITY_STATUS,
	/* end 9801-8555 */
	/* WayneWCShiue - 9801-62 - [FTM] Supprt FTM command */
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	/* end 9801-62 */
	/* WayneWCShiue - 9801-6414 - Add battery event for problem report */
	POWER_SUPPLY_PROP_MONITOR_EVENT,
	/* end 9801-6414 */
};

static int smb2_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;
	union power_supply_propval pval = {0, };
	union power_supply_propval test_capacity = {0, };	//The battery status show Full when battery capacity 100%.

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		//{Device was discharging when battery capacity is above 60% and temperature is warm.
		rc = smblib_get_prop_batt_status(chg, val);
		if (rc < 0)
			break;

		//{The battery status show Full when battery capacity 100%.
		rc = smblib_get_prop_batt_capacity(chg, &test_capacity);
		if (rc < 0)
			break;

		if (val->intval == POWER_SUPPLY_STATUS_CHARGING && test_capacity.intval == 100) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		}
		//}The battery status show Full when battery capacity 100%.

		/* When we force charging to be disabled, still report status as charging to userspace
		if (chg->razer_charge_limit_active && (val->intval == POWER_SUPPLY_STATUS_NOT_CHARGING
			|| val->intval == POWER_SUPPLY_STATUS_DISCHARGING)) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		} */

		if (val->intval == POWER_SUPPLY_STATUS_FULL ||
			val->intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
		{
			union power_supply_propval pval_health = {0, };
			union power_supply_propval pval_temp = {0, };
			union power_supply_propval pval_usb = {0, };
			union power_supply_propval pval_dc = {0, };
			union power_supply_propval pval_capacity = {0, };
			union power_supply_propval pval_is = {0, };

			rc |= smblib_get_prop_batt_health(chg, &pval_health);
			rc |= smblib_get_prop_from_bms(chg, POWER_SUPPLY_PROP_TEMP, &pval_temp);
			rc |= smblib_get_prop_usb_present(chg, &pval_usb);
			rc |= smblib_get_prop_dc_present(chg, &pval_dc);
			rc |= smblib_get_prop_batt_capacity(chg, &pval_capacity);
			rc |= smblib_get_prop_input_suspend(chg, &pval_is);

			//if (pval_temp.intval >= 430 && pval_is.intval == 0  &&
			if (pval_health.intval == POWER_SUPPLY_HEALTH_WARM && pval_is.intval == 0  &&
				pval_capacity.intval < 95 && pval_capacity.intval > 60 &&
				(pval_usb.intval == 1 || pval_dc.intval == 1))
			{
				pr_info("Change status to charging.  TEMP=%d HEALTH=%d IS=%d USB=%d DC=%d CAP=%d\n", pval_temp.intval, pval_health.intval, pval_is.intval, pval_usb.intval, pval_dc.intval, pval_capacity.intval);
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			}
		}
		break;
	case POWER_SUPPLY_PROP_STATUS_INTERNAL:
		//}Device was discharging when battery capacity is above 60% and temperature is warm.
		rc = smblib_get_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		rc = smblib_get_prop_batt_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_batt_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_get_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = smblib_get_prop_batt_charge_type(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_get_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		rc = smblib_get_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP:
		/* do not query RRADC if charger is not present */
		rc = smblib_get_prop_usb_present(chg, &pval);
		if (rc < 0)
			pr_err("Couldn't get usb present rc=%d\n", rc);

		rc = -ENODATA;
		if (pval.intval)
			rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
		rc = smblib_get_prop_charger_temp_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		val->intval = chg->step_chg_enabled;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		val->intval = chg->sw_jeita_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = get_client_vote(chg->fv_votable,
				BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_get_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		val->intval = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		val->intval = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_client_vote(chg->fcc_votable,
					      BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		val->intval = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		rc = smblib_get_prop_die_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		val->intval = chg->pulse_cnt;
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_TEMP:
		rc = smblib_get_prop_from_bms(chg, psp, val);
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		val->intval = chg->fcc_stepper_mode;
		break;
	/* WayneWCShiue - 9802-799 - Implement the WLC FCC adjust mechansim */
	case POWER_SUPPLY_PROP_FIH_WLC_FCC_EN:
		val->intval = chg->fih_wlc_fcc_en;
		break;
	/* end 9802-799 */
	/* WayneWCShiue - 9801-8555 - [BAT] Inform Battery Protect AP once the battery can only charge to 4.1V */
	case POWER_SUPPLY_PROP_JEITA_FULL_CAPACITY_STATUS:
		val->intval = FIH_check_chg_status(chg);
		break;
	/* end 9801-8555 */
	/* WayneWCShiue - 9801-62 - [FTM] Supprt FTM command */
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = get_effective_result(chg->usb_icl_votable);
		break;
	/* end 9801-62 */
	/* WayneWCShiue - 9801-6414 - Add battery event for problem report */
	case POWER_SUPPLY_PROP_MONITOR_EVENT:
		val->intval = collect_monitor_event(chg);
		break;
	/* end 9801-6414 */
	default:
		pr_err("batt power supply prop %d not supported\n", psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb2_batt_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	/* WayneWCShiue - 9801-5978 - open a back door to modify register */
	u8 value = 0;
	u8 mask = 0;
	u16 address = 0;
	/* end 9801-5978 */
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	/* WayneWCShiue - 9801-3730 - Change JEITA dynamically */
	struct smb2 *chip = power_supply_get_drvdata(psy);
	/* end 9801-3730 */

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		pr_debug("[BAT] INPUT_SUSPEND been set to %d", val->intval);
		rc = smblib_set_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		rc = smblib_set_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* WayneWCShiue - 9801-4573 - Open a back door to disable safety timer */
		if(val->intval == 95277259) {
			pr_err("Force to disable safety timer for QA testing\n");
			smblib_masked_write(chg, 0x10A0, GENMASK(1, 0), 0);
			break;
		}
		/* end 9801-4573 */

		/* WayneWCShiue - 9801-5978 - open a back door to modify register */
		if(val->intval > 100 && val->intval != 95277259) {
			address = (val->intval >> 16) & 0x0000FFFF;
			value = ((val->intval & 0x0000FF00) >> 8);
			mask = (val->intval & 0x000000FF);
			smblib_masked_write(chg, address, mask, value);
			break;
		}
		/* end 9801-5978 */

		rc = smblib_set_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		chg->batt_profile_fv_uv = val->intval;
		vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_set_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		vote(chg->fv_votable, QNOVO_VOTER,
			(val->intval >= 0), val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val->intval != -EINVAL && val->intval < 2000000, 0);
		if (val->intval == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		chg->step_chg_enabled = !!val->intval;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		if (chg->sw_jeita_enabled != (!!val->intval)) {
			rc = smblib_disable_hw_jeita(chg, !!val->intval);
			if (rc == 0)
				chg->sw_jeita_enabled = !!val->intval;
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		chg->batt_profile_fcc_ua = val->intval;
		vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val->intval)
			break;
		if (chg->pl.psy)
			power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		rc = smblib_rerun_aicl(chg);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		rc = smblib_dp_dm(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_set_prop_input_current_limited(chg, val);
		break;
	/* WayneWCShiue - 9801-3730 - Change JEITA dynamically */
	case POWER_SUPPLY_PROP_JEITA_DIFF_FN_EN:
		chg->diff_jeita_fn_en = (val->intval == 1) ? true : false;
		pr_err("Wayne: chg->diff_jeita_fn_en = %d\n", val->intval);
		FIH_adjust_JEITA(chg);
		break;
	case POWER_SUPPLY_PROP_JEITA_FCC_COOL:
		if(val->intval < 0)
			chg->jeita_fcc_comp_cool = chip->dt.jeita_fcc_comp;
		else
			chg->jeita_fcc_comp_cool = val->intval;
		pr_debug("chg->jeita_fcc_comp_cool = %d\n", chg->jeita_fcc_comp_cool);
		break;
	case POWER_SUPPLY_PROP_JEITA_FCC_WARM:
		if(val->intval < 0)
			chg->jeita_fcc_comp_warm = chip->dt.jeita_fcc_comp;
		else
			chg->jeita_fcc_comp_warm = val->intval;
		pr_debug("chg->jeita_fcc_comp_warm = %d\n", chg->jeita_fcc_comp_warm);
		break;
	case POWER_SUPPLY_PROP_JEITA_FV_COOL:
		if(val->intval < 0)
			chg->jeita_fv_comp_cool = chip->dt.jeita_fv_comp;
		else
			chg->jeita_fv_comp_cool = val->intval;
		pr_debug("chg->jeita_fv_comp_cool = %d\n", chg->jeita_fv_comp_cool);
		break;
	case POWER_SUPPLY_PROP_JEITA_FV_WARM:
		if(val->intval < 0)
			chg->jeita_fv_comp_warm = chip->dt.jeita_fv_comp;
		else
			chg->jeita_fv_comp_warm = val->intval;
		pr_debug("chg->jeita_fv_comp_warm = %d\n", chg->jeita_fv_comp_warm);
		break;
	/* end 9801-3730 */
	/* WayneWCShiue - 9801-8555 - [BAT] Inform Battery Protect AP once the battery can only charge to 4.1V */
	case POWER_SUPPLY_PROP_JEITA_FULL_CAPACITY_WARM_EN:
		if(val->intval == 1)
			chg->fih_jeita_full_capacity_warm_en = true;
		 else
			chg->fih_jeita_full_capacity_warm_en = false;
		pr_debug("fih_jeita_full_capacity_warm_en = %s\n", val->intval == 1 ? "true" : "false");
		break;
	case POWER_SUPPLY_PROP_JEITA_FULL_CAPACITY_COOL_EN:
		if(val->intval == 1)
			chg->fih_jeita_full_capacity_cool_en = true;
		 else
			chg->fih_jeita_full_capacity_cool_en = false;
		pr_debug("fih_jeita_full_capacity_cool_en = %s\n", val->intval == 1 ? "true" : "false");
		break;
	/* end 9801-8555 */
	/* WayneWCShiue - 9802-1713 - Add periodical checker mechanism for charging */
	case POWER_SUPPLY_PROP_FIH_PERIOD_CHECKER:
		FIH_adjust_JEITA(chg);
		FIH_soft_JEITA_recharge_check(chg);
		FIH_chg_abnormal_check(chg);
	break;
	/* end 9802-1713 */
	/* WayneWCShiue - 9801-62 - [FTM] Supprt FTM command */
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		/* Use icl function to instead of vote mechanism, because the new icl vote_callback cannot allow to set the icl which is smaller than aicl_result */
		smblib_set_icl_current(chg, val->intval);

		/* Disable THERMAL_DAEMON_VOTER, only let DEFAULT_VOTER has the ability to change FCC */
		vote(chg->fcc_votable, THERMAL_DAEMON_VOTER, false, 0);
		break;
	/* end 9801-62 */
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int smb2_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
	case POWER_SUPPLY_PROP_DP_DM:
	case POWER_SUPPLY_PROP_RERUN_AICL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
	/* WayneWCShiue - 9801-62 - [FTM] Supprt FTM command */
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	/* end 9801-62 */
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smb2_batt_props,
	.num_properties = ARRAY_SIZE(smb2_batt_props),
	.get_property = smb2_batt_get_prop,
	.set_property = smb2_batt_set_prop,
	.property_is_writeable = smb2_batt_prop_is_writeable,
};

static int smb2_init_batt_psy(struct smb2 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = power_supply_register(chg->dev,
						   &batt_psy_desc,
						   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}

/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb2_vbus_reg_ops = {
	.enable = smblib_vbus_regulator_enable,
	.disable = smblib_vbus_regulator_disable,
	.is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb2_init_vbus_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
				      GFP_KERNEL);
	if (!chg->vbus_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vbus_vreg->rdesc.owner = THIS_MODULE;
	chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vbus_vreg->rdesc.ops = &smb2_vbus_reg_ops;
	chg->vbus_vreg->rdesc.of_match = "qcom,smb2-vbus";
	chg->vbus_vreg->rdesc.name = "qcom,smb2-vbus";

	chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vbus_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vbus_vreg->rdev)) {
		rc = PTR_ERR(chg->vbus_vreg->rdev);
		chg->vbus_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VBUS regualtor rc=%d\n", rc);
	}

	return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb2_vconn_reg_ops = {
	.enable = smblib_vconn_regulator_enable,
	.disable = smblib_vconn_regulator_disable,
	.is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb2_init_vconn_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	if (chg->micro_usb_mode)
		return 0;

	chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
				      GFP_KERNEL);
	if (!chg->vconn_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vconn_vreg->rdesc.owner = THIS_MODULE;
	chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vconn_vreg->rdesc.ops = &smb2_vconn_reg_ops;
	chg->vconn_vreg->rdesc.of_match = "qcom,smb2-vconn";
	chg->vconn_vreg->rdesc.name = "qcom,smb2-vconn";

	chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vconn_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vconn_vreg->rdev)) {
		rc = PTR_ERR(chg->vconn_vreg->rdev);
		chg->vconn_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VCONN regualtor rc=%d\n", rc);
	}

	return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
static int smb2_config_wipower_input_power(struct smb2 *chip, int uw)
{
	int rc;
	int ua;
	struct smb_charger *chg = &chip->chg;
	s64 nw = (s64)uw * 1000;

	if (uw < 0)
		return 0;

	ua = div_s64(nw, ZIN_ICL_PT_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_PT_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_hv rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int smb2_configure_typec(struct smb_charger *chg)
{
	int rc;

	/*
	 * trigger the usb-typec-change interrupt only when the CC state
	 * changes
	 */
	rc = smblib_write(chg, TYPE_C_INTRPT_ENB_REG,
			  TYPEC_CCSTATE_CHANGE_INT_EN_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/*
	 * disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure Type-C rc=%d\n", rc);
		return rc;
	}

	/* increase VCONN softstart */
	rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG,
			VCONN_SOFTSTART_CFG_MASK, VCONN_SOFTSTART_CFG_MASK);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't increase VCONN softstart rc=%d\n",
			rc);
		return rc;
	}

	/* disable try.SINK mode and legacy cable IRQs */
	rc = smblib_masked_write(chg, TYPE_C_CFG_3_REG, EN_TRYSINK_MODE_BIT |
				TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT |
				TYPEC_LEGACY_CABLE_INT_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set Type-C config rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_disable_typec(struct smb_charger *chg)
{
	int rc;

	/* Move to typeC mode */
	/* configure FSM in idle state and disable UFP_ENABLE bit */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT | UFP_EN_CMD_BIT,
			TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure TypeC mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to start */
	msleep(100);
	/* move to uUSB mode */
	/* configure FSM in idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure micro USB mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, TYPE_C_OR_U_USB_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_init_hw(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;
	u8 stat, val;

	if (chip->dt.no_battery)
		chg->fake_capacity = 50;

	if (chg->batt_profile_fcc_ua < 0)
		smblib_get_charge_param(chg, &chg->param.fcc,
				&chg->batt_profile_fcc_ua);

	if (chg->batt_profile_fv_uv < 0)
		smblib_get_charge_param(chg, &chg->param.fv,
				&chg->batt_profile_fv_uv);

	smblib_get_charge_param(chg, &chg->param.usb_icl,
				&chg->default_icl_ua);
	if (chip->dt.usb_icl_ua < 0)
		chip->dt.usb_icl_ua = chg->default_icl_ua;

	if (chip->dt.dc_icl_ua < 0)
		smblib_get_charge_param(chg, &chg->param.dc_icl,
					&chip->dt.dc_icl_ua);

	if (chip->dt.min_freq_khz > 0) {
		chg->param.freq_buck.min_u = chip->dt.min_freq_khz;
		chg->param.freq_boost.min_u = chip->dt.min_freq_khz;
	}

	if (chip->dt.max_freq_khz > 0) {
		chg->param.freq_buck.max_u = chip->dt.max_freq_khz;
		chg->param.freq_boost.max_u = chip->dt.max_freq_khz;
	}

	/* set a slower soft start setting for OTG */
	rc = smblib_masked_write(chg, DC_ENG_SSUPPLY_CFG2_REG,
				ENG_SSUPPLY_IVREF_OTG_SS_MASK, OTG_SS_SLOW);
	if (rc < 0) {
		pr_err("Couldn't set otg soft start rc=%d\n", rc);
		return rc;
	}

	/* set OTG current limit */
	rc = smblib_set_charge_param(chg, &chg->param.otg_cl,
				(chg->wa_flags & OTG_WA) ?
				chg->param.otg_cl.min_u : chg->otg_cl_ua);
	if (rc < 0) {
		pr_err("Couldn't set otg current limit rc=%d\n", rc);
		return rc;
	}

	/* WayneWCShiue - 9801-468 - [BAT] Jeita temperature protection */
	if (chip->dt.jeita_fcc_comp < 0)
		smblib_get_charge_param(chg, &chg->param.jeita_cc_comp,
					&chip->dt.jeita_fcc_comp);

	if (chip->dt.jeita_fv_comp < 0)
		smblib_get_charge_param(chg, &chg->param.jeita_fv_comp,
					&chip->dt.jeita_fv_comp);
	/* end 9801-468 */

	/* DY-FixMergeConflict chg->otg_cl_ua = chip->dt.otg_cl_ua; */
	/* DY-FixMergeConflict chg->dcp_icl_ua = chip->dt.usb_icl_ua; */
	chg->boost_threshold_ua = chip->dt.boost_threshold_ua;

	rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read APSD_RESULT_STATUS rc=%d\n", rc);
		return rc;
	}

	smblib_rerun_apsd_if_required(chg);

	/* clear the ICL override if it is set */
	if (smblib_icl_override(chg, false) < 0) {
		pr_err("Couldn't disable ICL override rc=%d\n", rc);
		return rc;
	}

	/* votes must be cast before configuring software control */
	/* vote 0mA on usb_icl for non battery platforms */
	vote(chg->usb_icl_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->dc_suspend_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->fcc_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fcc_ua);
	vote(chg->fv_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fv_uv);
	vote(chg->dc_icl_votable,
		DEFAULT_VOTER, true, chip->dt.dc_icl_ua);
	vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER,
			true, 0);
	vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER,
			true, 0);
	vote(chg->hvdcp_disable_votable_indirect, DEFAULT_VOTER,
		chip->dt.hvdcp_disable, 0);
	vote(chg->pd_disallowed_votable_indirect, CC_DETACHED_VOTER,
			true, 0);
	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
			true, 0);
	vote(chg->pd_disallowed_votable_indirect, MICRO_USB_VOTER,
			chg->micro_usb_mode, 0);
	vote(chg->hvdcp_enable_votable, MICRO_USB_VOTER,
			chg->micro_usb_mode, 0);

	/*
	 * AICL configuration:
	 * start from min and AICL ADC disable
	 */
	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
	/* WayneWCShiue - 9801-3086 - Enable AICL re-run */
			USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_RERUN_EN_BIT
				| USBIN_AICL_ADC_EN_BIT, USBIN_AICL_RERUN_EN_BIT);
	/* end 9801-3086 */
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
		return rc;
	}

	//{Fixed charging stopped abnormally
	/* WayneWCShiue - 9801-3086 - Enable AICL re-run */
	rc = smblib_masked_write(chg, AICL_RERUN_TIME_CFG_REG,
			AICL_RERUN_TIME_MASK,  0x1);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure AICL rerun timer rc=%d\n", rc);
	}
	/* end 9801-3086 */
	//}Fixed charging stopped abnormally

	/* Configure charge enable for software control; active high */
	rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				 CHG_EN_POLARITY_BIT |
				 CHG_EN_SRC_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charger rc=%d\n", rc);
		return rc;
	}

	/* enable the charging path */
	rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
		return rc;
	}

	if (chg->micro_usb_mode)
		rc = smb2_disable_typec(chg);
	else
		rc = smb2_configure_typec(chg);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/* configure VCONN for software control */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
				 VCONN_EN_SRC_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VCONN for SW control rc=%d\n", rc);
		return rc;
	}

	/* configure VBUS for software control */
	rc = smblib_masked_write(chg, OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VBUS for SW control rc=%d\n", rc);
		return rc;
	}

	val = (ilog2(chip->dt.wd_bark_time / 16) << BARK_WDOG_TIMEOUT_SHIFT) &
						BARK_WDOG_TIMEOUT_MASK;
	val |= BITE_WDOG_TIMEOUT_8S;
	rc = smblib_masked_write(chg, SNARL_BARK_BITE_WD_CFG_REG,
			BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
			BARK_WDOG_TIMEOUT_MASK | BITE_WDOG_TIMEOUT_MASK,
			val);
	if (rc) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* enable WD BARK and enable it on plugin */
	rc = smblib_masked_write(chg, WD_CFG_REG,
			WATCHDOG_TRIGGER_AFP_EN_BIT |
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT,
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT);
	if (rc) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* configure wipower watts */
	rc = smb2_config_wipower_input_power(chip, chip->dt.wipower_max_uw);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure wipower rc=%d\n", rc);
		return rc;
	}

	/* disable SW STAT override */
	rc = smblib_masked_write(chg, STAT_CFG_REG,
				 STAT_SW_OVERRIDE_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't disable SW STAT override rc=%d\n",
			rc);
		return rc;
	}

	/* disable h/w autonomous parallel charging control */
	rc = smblib_masked_write(chg, MISC_CFG_REG,
				 STAT_PARALLEL_1400MA_EN_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't disable h/w autonomous parallel control rc=%d\n",
			rc);
		return rc;
	}

	/*
	 * allow DRP.DFP time to exceed by tPDdebounce time.
	 */
	rc = smblib_masked_write(chg, TAPER_TIMER_SEL_CFG_REG,
				TYPEC_DRP_DFP_TIME_CFG_BIT,
				TYPEC_DRP_DFP_TIME_CFG_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DRP.DFP time rc=%d\n",
			rc);
		return rc;
	}

	/* configure float charger options */
	switch (chip->dt.float_option) {
	case 1:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, 0);
		break;
	case 2:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FORCE_FLOAT_SDP_CFG_BIT);
		break;
	case 3:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FLOAT_DIS_CHGING_CFG_BIT);
		break;
	case 4:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, SUSPEND_FLOAT_CFG_BIT);
		break;
	default:
		rc = 0;
		break;
	}

//@TEST_ :
#ifdef CONFIG_FIH_9802
	rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG, 0x20, 0);
#endif
//@_TEST :

	/* WayneWCShiue - 9801-468 - [BAT] Jeita temperature protection */
	rc = smblib_set_charge_param(chg, &chg->param.jeita_cc_comp, chip->dt.jeita_fcc_comp);
	if (rc < 0) {
		pr_err("Wayne: Couldn't configure jeita fcc comp rc = %d\n", rc);
		return rc;
	}

	rc = smblib_set_charge_param(chg, &chg->param.jeita_fv_comp, chip->dt.jeita_fv_comp);
	if (rc < 0) {
		pr_err("Wayne: Couldn't configure jeita fv comp rc = %d\n", rc);
		return rc;
	}
	/* end 9801-468 */

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure float charger options rc=%d\n",
			rc);
		return rc;
	}

	rc = smblib_read(chg, USBIN_OPTIONS_2_CFG_REG, &chg->float_cfg);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read float charger options rc=%d\n",
			rc);
		return rc;
	}

	switch (chip->dt.chg_inhibit_thr_mv) {
	case 50:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_50MV);
		break;
	case 100:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_100MV);
		break;
	case 200:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_200MV);
		break;
	case 300:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_300MV);
		break;
	case 0:
		rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				CHARGER_INHIBIT_BIT, 0);
	default:
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
			rc);
		return rc;
	}

	if (chip->dt.auto_recharge_soc) {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	} else {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	}

	if (chg->sw_jeita_enabled) {
		rc = smblib_disable_hw_jeita(chg, true);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't set hw jeita rc=%d\n", rc);
			return rc;
		}
	}

	return rc;
}

static int smb2_post_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;

	/* In case the usb path is suspended, we would have missed disabling
	 * the icl change interrupt because the interrupt could have been
	 * not requested
	 */
	rerun_election(chg->usb_icl_votable);

	/* configure power role for dual-role */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure power role for DRP rc=%d\n", rc);
		return rc;
	}

	rerun_election(chg->usb_irq_enable_votable);

	return 0;
}

static int smb2_chg_config_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct pmic_revid_data *pmic_rev_id;
	struct device_node *revid_dev_node;

	revid_dev_node = of_parse_phandle(chip->chg.dev->of_node,
					  "qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_err("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	pmic_rev_id = get_revid_data(revid_dev_node);
	if (IS_ERR_OR_NULL(pmic_rev_id)) {
		/*
		 * the revid peripheral must be registered, any failure
		 * here only indicates that the rev-id module has not
		 * probed yet.
		 */
		return -EPROBE_DEFER;
	}

	switch (pmic_rev_id->pmic_subtype) {
	case PMI8998_SUBTYPE:
		chip->chg.smb_version = PMI8998_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | QC_AUTH_INTERRUPT_WA_BIT
				| TYPEC_PBS_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V1P1_REV4) /* PMI rev 1.1 */
			chg->wa_flags |= QC_CHARGER_DETECTION_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V2P0_REV4) /* PMI rev 2.0 */
			chg->wa_flags |= TYPEC_CC2_REMOVAL_WA_BIT;
		chg->chg_freq.freq_5V		= 600;
		chg->chg_freq.freq_6V_8V	= 800;
		chg->chg_freq.freq_9V		= 1000;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1000;
		chg->chg_freq.freq_below_otg_threshold = 2000;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	case PM660_SUBTYPE:
		chip->chg.smb_version = PM660_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | OTG_WA | OV_IRQ_WA_BIT
				| TYPEC_PBS_WA_BIT;
		chg->param.freq_buck = pm660_params.freq_buck;
		chg->param.freq_boost = pm660_params.freq_boost;
		chg->chg_freq.freq_5V		= 650;
		chg->chg_freq.freq_6V_8V	= 850;
		chg->chg_freq.freq_9V		= 1050;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1050;
		chg->chg_freq.freq_below_otg_threshold = 1600;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	default:
		pr_err("PMIC subtype %d not supported\n",
				pmic_rev_id->pmic_subtype);
		return -EINVAL;
	}

	return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb2_determine_initial_status(struct smb2 *chip)
{
	struct smb_irq_data irq_data = {chip, "determine-initial-status"};
	struct smb_charger *chg = &chip->chg;

	if (chg->bms_psy)
		smblib_suspend_on_debug_battery(chg);
	smblib_handle_usb_plugin(0, &irq_data);
	smblib_handle_usb_typec_change(0, &irq_data);
	smblib_handle_usb_source_change(0, &irq_data);
	smblib_handle_chg_state_change(0, &irq_data);
	smblib_handle_icl_change(0, &irq_data);
	smblib_handle_batt_temp_changed(0, &irq_data); //Device was discharging when battery capacity is above 60% and temperature is warm.
	smblib_handle_wdog_bark(0, &irq_data);

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb2_irqs[] = {
/* CHARGER IRQs */
	[CHG_ERROR_IRQ] = {
		.name		= "chg-error",
		.handler	= smblib_handle_debug,
	},
	[CHG_STATE_CHANGE_IRQ] = {
		.name		= "chg-state-change",
		.handler	= smblib_handle_chg_state_change,
		.wake		= true,
	},
	[STEP_CHG_STATE_CHANGE_IRQ] = {
		.name		= "step-chg-state-change",
		.handler	= NULL,
	},
	[STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
		.name		= "step-chg-soc-update-fail",
		.handler	= NULL,
	},
	[STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
		.name		= "step-chg-soc-update-request",
		.handler	= NULL,
	},
/* OTG IRQs */
	[OTG_FAIL_IRQ] = {
		.name		= "otg-fail",
		.handler	= smblib_handle_debug,
	},
	[OTG_OVERCURRENT_IRQ] = {
		.name		= "otg-overcurrent",
		.handler	= smblib_handle_otg_overcurrent,
	},
	[OTG_OC_DIS_SW_STS_IRQ] = {
		.name		= "otg-oc-dis-sw-sts",
		.handler	= smblib_handle_debug,
	},
	[TESTMODE_CHANGE_DET_IRQ] = {
		.name		= "testmode-change-detect",
		.handler	= smblib_handle_debug,
	},
/* BATTERY IRQs */
	[BATT_TEMP_IRQ] = {
		.name		= "bat-temp",
		.handler	= smblib_handle_batt_temp_changed,
		/* WayneWCShiue - 9801-7789 - bat-temp need to wakeup system, and change JEITA setting */
		.wake		= true,
		/* end 9801-7789 */
	},
	[BATT_OCP_IRQ] = {
		.name		= "bat-ocp",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_OV_IRQ] = {
		.name		= "bat-ov",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_LOW_IRQ] = {
		.name		= "bat-low",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_THERM_ID_MISS_IRQ] = {
		.name		= "bat-therm-or-id-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_TERM_MISS_IRQ] = {
		.name		= "bat-terminal-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
/* USB INPUT IRQs */
	[USBIN_COLLAPSE_IRQ] = {
		.name		= "usbin-collapse",
		.handler	= smblib_handle_debug,
	},
	[USBIN_LT_3P6V_IRQ] = {
		.name		= "usbin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[USBIN_UV_IRQ] = {
		.name		= "usbin-uv",
		.handler	= smblib_handle_usbin_uv,
	},
	[USBIN_OV_IRQ] = {
		.name		= "usbin-ov",
		.handler	= smblib_handle_debug,
	},
	[USBIN_PLUGIN_IRQ] = {
		.name		= "usbin-plugin",
		.handler	= smblib_handle_usb_plugin,
		.wake		= true,
	},
	[USBIN_SRC_CHANGE_IRQ] = {
		.name		= "usbin-src-change",
		.handler	= smblib_handle_usb_source_change,
		.wake		= true,
	},
	[USBIN_ICL_CHANGE_IRQ] = {
		.name		= "usbin-icl-change",
		.handler	= smblib_handle_icl_change,
		.wake		= true,
	},
	[TYPE_C_CHANGE_IRQ] = {
		.name		= "type-c-change",
		.handler	= smblib_handle_usb_typec_change,
		.wake		= true,
	},
/* DC INPUT IRQs */
	[DCIN_COLLAPSE_IRQ] = {
		.name		= "dcin-collapse",
		.handler	= smblib_handle_debug,
	},
	[DCIN_LT_3P6V_IRQ] = {
		.name		= "dcin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[DCIN_UV_IRQ] = {
		.name		= "dcin-uv",
		.handler	= smblib_handle_debug,
	},
	[DCIN_OV_IRQ] = {
		.name		= "dcin-ov",
		.handler	= smblib_handle_debug,
	},
	[DCIN_PLUGIN_IRQ] = {
		.name		= "dcin-plugin",
		.handler	= smblib_handle_dc_plugin,
		.wake		= true,
	},
	[DIV2_EN_DG_IRQ] = {
		.name		= "div2-en-dg",
		.handler	= smblib_handle_debug,
	},
	[DCIN_ICL_CHANGE_IRQ] = {
		.name		= "dcin-icl-change",
		.handler	= smblib_handle_debug,
	},
/* MISCELLANEOUS IRQs */
	[WDOG_SNARL_IRQ] = {
		.name		= "wdog-snarl",
		.handler	= NULL,
	},
	[WDOG_BARK_IRQ] = {
		.name		= "wdog-bark",
		.handler	= smblib_handle_wdog_bark,
		.wake		= true,
	},
	[AICL_FAIL_IRQ] = {
		.name		= "aicl-fail",
		.handler	= smblib_handle_debug,
	},
	[AICL_DONE_IRQ] = {
		.name		= "aicl-done",
		.handler	= smblib_handle_debug,
	},
	[HIGH_DUTY_CYCLE_IRQ] = {
		.name		= "high-duty-cycle",
		.handler	= smblib_handle_high_duty_cycle,
		.wake		= true,
	},
	[INPUT_CURRENT_LIMIT_IRQ] = {
		.name		= "input-current-limiting",
		.handler	= smblib_handle_debug,
	},
	[TEMPERATURE_CHANGE_IRQ] = {
		.name		= "temperature-change",
		.handler	= smblib_handle_debug,
	},
	[SWITCH_POWER_OK_IRQ] = {
		.name		= "switcher-power-ok",
		.handler	= smblib_handle_switcher_power_ok,
		.storm_data	= {true, 1000, 8},
	},
};

static int smb2_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (strcmp(smb2_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb2_request_interrupt(struct smb2 *chip,
				struct device_node *node, const char *irq_name)
{
	struct smb_charger *chg = &chip->chg;
	int rc, irq, irq_index;
	struct smb_irq_data *irq_data;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb2_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb2_irqs[irq_index].handler)
		return 0;

	irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	irq_data->parent_data = chip;
	irq_data->name = irq_name;
	irq_data->storm_data = smb2_irqs[irq_index].storm_data;
	mutex_init(&irq_data->storm_data.storm_lock);

	rc = devm_request_threaded_irq(chg->dev, irq, NULL,
					smb2_irqs[irq_index].handler,
					IRQF_ONESHOT, irq_name, irq_data);
	if (rc < 0) {
		pr_err("Couldn't request irq %d\n", irq);
		return rc;
	}

	smb2_irqs[irq_index].irq = irq;
	smb2_irqs[irq_index].irq_data = irq_data;
	if (smb2_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb2_request_interrupts(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					    prop, name) {
			rc = smb2_request_interrupt(chip, child, name);
			if (rc < 0)
				return rc;
		}
	}
	if (chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq)
		chg->usb_icl_change_irq_enabled = true;

	return rc;
}

static void smb2_free_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0) {
			if (smb2_irqs[i].wake)
				disable_irq_wake(smb2_irqs[i].irq);

			devm_free_irq(chg->dev, smb2_irqs[i].irq,
					smb2_irqs[i].irq_data);
		}
	}
}

static void smb2_disable_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0)
			disable_irq(smb2_irqs[i].irq);
	}
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->batt_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_batt_psy_update_ops, NULL,
			force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->usb_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_usb_psy_update_ops, NULL,
			force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->dc_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_dc_psy_update_ops, NULL,
			force_dc_psy_update_write, "0x%02llx\n");

static void smb2_create_debugfs(struct smb2 *chip)
{
	struct dentry *file;

	chip->dfs_root = debugfs_create_dir("charger", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create charger debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	file = debugfs_create_file("force_batt_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_batt_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_usb_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_usb_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_dc_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_dc_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
			(long)file);
}

#else

static void smb2_create_debugfs(struct smb2 *chip)
{}

#endif

static int smb2_probe(struct platform_device *pdev)
{
	struct smb2 *chip;
	struct smb_charger *chg;
	int rc = 0;
	union power_supply_propval val;
	int usb_present, batt_present, batt_health, batt_charge_type;
	#ifdef BBS_LOG
	u8 stat = 0;
	#endif

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chg = &chip->chg;
	chg->dev = &pdev->dev;
	chg->param = v1_params;
	chg->debug_mask = &__debug_mask;
	chg->try_sink_enabled = &__try_sink_enabled;
	chg->weak_chg_icl_ua = &__weak_chg_icl_ua;
	chg->mode = PARALLEL_MASTER;
	chg->irq_info = smb2_irqs;
	chg->name = "PMI";

	chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
	if (!chg->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	rc = smb2_chg_config_init(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't setup chg_config rc=%d\n", rc);
		return rc;
	}

	rc = smb2_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	rc = smblib_init(chg);
	if (rc < 0) {
		pr_err("Smblib_init failed rc=%d\n", rc);
		goto cleanup;
	}

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

	rc = smb2_init_vbus_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vbus regulator rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_init_vconn_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vconn regulator rc=%d\n",
				rc);
		goto cleanup;
	}

	/* extcon registration */
	chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
	if (IS_ERR(chg->extcon)) {
		rc = PTR_ERR(chg->extcon);
		dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(chg->dev, chg->extcon);
	if (rc < 0) {
		dev_err(chg->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = smb2_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_dc_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize dc psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_port_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_post_init(chip);
	if (rc < 0) {
		pr_err("Failed in post init rc=%d\n", rc);
		goto cleanup;
	}

	smb2_create_debugfs(chip);

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		goto cleanup;
	}
	usb_present = val.intval;

	rc = smblib_get_prop_batt_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt present rc=%d\n", rc);
		goto cleanup;
	}
	batt_present = val.intval;

	rc = smblib_get_prop_batt_health(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt health rc=%d\n", rc);
		val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	batt_health = val.intval;

	rc = smblib_get_prop_batt_charge_type(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		goto cleanup;
	}
	batt_charge_type = val.intval;

	dev_set_name(chg->dev, "qpnp-smb2");
	device_init_wakeup(chg->dev, true);

	pr_info("QPNP SMB2 probed successfully usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
		usb_present, chg->real_charger_type,
		batt_present, batt_health, batt_charge_type);
	/* WayneWCShiue - 9801-61 - [BAT] Workaround for DUT cannot be charged with pmi8998 v2 */
	#ifdef CONFIG_FIH_9801
	rc = smblib_masked_write(chg, THERMREG_SRC_CFG_REG,
	THERMREG_SKIN_ADC_SRC_EN_BIT,
	0);
	if (rc < 0) {
		pr_err("Couldn't configure THERMREG_SRC_CFG_REG rc=%d\n", rc);
		return rc;
	}
	#endif
	/* end 9801-61 */

	#ifdef BBS_LOG
	rc = smblib_read(chg, 0x8C8, &stat);
	if (rc < 0) {
		pr_err("Couldn't read UVLO state rc=%d\n", rc);
	} else {
		if(stat & BIT(6))
			printk("BBox::UEC;49::0\n");
	}
	#endif

/* FIH - SimonSSChang - Disable QC Wi-Power config */
#if defined(CONFIG_FIH_9802)
	/* AICL configuration */
	smblib_write(chg, DCIN_AICL_OPTIONS_CFG_REG, 0x7C);
	/* Lower AICL collapse threshold */
	smblib_write(chg, DCIN_AICL_REF_SEL_CFG_REG, 0x02);
	/* Disable Wi-Power */
	smblib_write(chg, WI_PWR_OPTIONS_REG, 0x00);
	printk(KERN_INFO "Disable Wi-Power option\n");
#endif
/* end FIH */

/* FIH - akckwang - 9801-506 - Create a node to on/off otg */
#if defined(CONFIG_FIH_9801) || defined(CONFIG_FIH_9802)
	device_create_file(&pdev->dev, &dev_attr_fih_otg_fun); // FIHTDC, IdaChiang, add for FREQ
	mChip = chip;
#endif
/* end FIH - 9801-506 */

	device_create_file(&pdev->dev, &dev_attr_razer_charge_limit_enable);
	device_create_file(&pdev->dev, &dev_attr_razer_charge_limit_max);
	device_create_file(&pdev->dev, &dev_attr_razer_charge_limit_dropdown);

	pr_info("QPNP SMB2 probed successfully\n");
	return rc;

cleanup:
	smb2_free_interrupts(chg);
	if (chg->batt_psy)
		power_supply_unregister(chg->batt_psy);
	if (chg->usb_main_psy)
		power_supply_unregister(chg->usb_main_psy);
	if (chg->usb_psy)
		power_supply_unregister(chg->usb_psy);
	if (chg->usb_port_psy)
		power_supply_unregister(chg->usb_port_psy);
	if (chg->dc_psy)
		power_supply_unregister(chg->dc_psy);
	if (chg->vconn_vreg && chg->vconn_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vconn_vreg->rdev);
	if (chg->vbus_vreg && chg->vbus_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vbus_vreg->rdev);

	smblib_deinit(chg);

	platform_set_drvdata(pdev, NULL);
	return rc;
}

static int smb2_remove(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	power_supply_unregister(chg->batt_psy);
	power_supply_unregister(chg->usb_psy);
	power_supply_unregister(chg->usb_port_psy);
	regulator_unregister(chg->vconn_vreg->rdev);
	regulator_unregister(chg->vbus_vreg->rdev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void smb2_shutdown(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	/* disable all interrupts */
	smb2_disable_interrupts(chg);

	/* configure power role for UFP */
	smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				TYPEC_POWER_ROLE_CMD_MASK, UFP_EN_CMD_BIT);

	/* force HVDCP to 5V */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT, 0);
	smblib_write(chg, CMD_HVDCP_2_REG, FORCE_5V_BIT);

	/* force enable APSD */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				 AUTO_SRC_DETECT_BIT, AUTO_SRC_DETECT_BIT);
}

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,qpnp-smb2", },
	{ },
};

static struct platform_driver smb2_driver = {
	.driver		= {
		.name		= "qcom,qpnp-smb2",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
	},
	.probe		= smb2_probe,
	.remove		= smb2_remove,
	.shutdown	= smb2_shutdown,
};
module_platform_driver(smb2_driver);

MODULE_DESCRIPTION("QPNP SMB2 Charger Driver");
MODULE_LICENSE("GPL v2");