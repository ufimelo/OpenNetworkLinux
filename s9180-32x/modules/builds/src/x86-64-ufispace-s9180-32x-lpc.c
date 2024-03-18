/*
 * A lpc driver for the ufispace_s9180_32x
 *
 * Copyright (C) 2022 UfiSpace Technology Corporation.
 * Melo Lin <melo.lin@ufispace.com>
 *
 * Based on ad7414.c
 * Copyright 2006 Stefan Roese <sr at denx.de>, DENX Software Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hwmon-sysfs.h>
#include <linux/gpio.h>

#if !defined(SENSOR_DEVICE_ATTR_RO)
#define SENSOR_DEVICE_ATTR_RO(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0444, _func##_show, NULL, _index)
#endif

#if !defined(SENSOR_DEVICE_ATTR_RW)
#define SENSOR_DEVICE_ATTR_RW(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0644, _func##_show, _func##_store, _index)

#endif

#if !defined(SENSOR_DEVICE_ATTR_WO)
#define SENSOR_DEVICE_ATTR_WO(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0200, NULL, _func##_store, _index)
#endif

#define BSP_LOG_R(fmt, args...) \
    _bsp_log (LOG_READ, KERN_INFO "%s:%s[%d]: " fmt "\r\n", \
            __FILE__, __func__, __LINE__, ##args)
#define BSP_LOG_W(fmt, args...) \
    _bsp_log (LOG_WRITE, KERN_INFO "%s:%s[%d]: " fmt "\r\n", \
            __FILE__, __func__, __LINE__, ##args)

#define _DEVICE_ATTR(_name)     \
    &sensor_dev_attr_##_name.dev_attr.attr

#define BSP_PR(level, fmt, args...) _bsp_log (LOG_SYS, level "[BSP]" fmt "\r\n", ##args)

#define DRIVER_NAME "x86_64_ufispace_s9180_32x_lpc"

/* LPC registers */
#define REG_BASE_MB                       0x600
#define REG_NONE                          0x00

//CPU CPLD
#define REG_CPU_CPLD_VERSION              (REG_BASE_MB + 0x00)
#define REG_BMC_SIGNAL_STATUS             (REG_BASE_MB + 0x01)
#define REG_BIOS_MUX_SEL                  (REG_BASE_MB + 0x02)
#define REG_RESET_SIGNAL                  (REG_BASE_MB + 0x03)
#define REG_WATCHDOG_ENABLE               (REG_BASE_MB + 0x04)
#define REG_MASK                          (REG_BASE_MB + 0x05)

// BMC mailbox
#define REG_TEMP_MAC_HWM                  (REG_BASE_MB + 0xC0)

#define MASK_ALL             (0xFF)
#define MASK_NONE            (0x00)
#define MASK_0000_0001       (0x01)
#define MASK_0000_0011       (0x03)
#define MASK_0000_0100       (0x04)
#define MASK_0000_0010       (0x02)
#define MASK_0000_0111       (0x07)
#define MASK_0000_1111       (0x0F)
#define MASK_0001_1000       (0x18)
#define MASK_0001_0000       (0x10)
#define MASK_0010_0000       (0x20)
#define MASK_0011_0111       (0x37)
#define MASK_0011_1111       (0x3F)
#define MASK_0100_0000       (0x40)
#define MASK_1000_0000       (0x80)
#define MASK_1100_0000       (0xC0)

#define LPC_MDELAY                        (5)
#define MDELAY_RESET_INTERVAL             (100)
#define MDELAY_RESET_FINISH               (500)

/* LPC sysfs attributes index  */
enum lpc_sysfs_attributes {
    //CPU CPLD
    ATT_CPU_CPLD_VERSION,
    ATT_BMC_CPU_NMI,
    ATT_BMC_CPU_INT,
    ATT_CPU_MB_RST,
    ATT_CPU_BMC_RST,
    ATT_CPU_MM_INT,
    ATT_BIOS_MUX_SEL,
    ATT_WDT_DOG_ENABLE,
    ATT_WDT_RCVRY_ENABLE,
    ATT_BMC_CPU_INT_MASK,
    ATT_CPLD_PCIE_RST_DISABLE,


    //BMC mailbox
    ATT_TEMP_MAC_HWM,

    //BSP
    ATT_BSP_VERSION,
    ATT_BSP_DEBUG,
    ATT_BSP_PR_INFO,
    ATT_BSP_PR_ERR,
    ATT_BSP_REG,
    ATT_BSP_REG_VALUE,
    ATT_BSP_GPIO_MAX,
    ATT_MAX
};

enum data_type {
    DATA_HEX,
    DATA_DEC,
    DATA_S_DEC,
    DATA_UNK,
};

typedef struct  {
    u16 reg;
    u8 mask;
    u8 data_type;
} attr_reg_map_t;

attr_reg_map_t attr_reg[]= {

    //CPU CPLD
    [ATT_CPU_CPLD_VERSION]     = {REG_CPU_CPLD_VERSION  , MASK_0000_1111, DATA_DEC},
    [ATT_BMC_CPU_NMI]          = {REG_BMC_SIGNAL_STATUS , MASK_1000_0000, DATA_HEX},
    [ATT_BMC_CPU_INT]          = {REG_BMC_SIGNAL_STATUS , MASK_0100_0000, DATA_HEX},
    [ATT_CPU_MB_RST]           = {REG_RESET_SIGNAL      , MASK_1000_0000, DATA_HEX},
    [ATT_CPU_BMC_RST]          = {REG_RESET_SIGNAL      , MASK_0010_0000, DATA_HEX},
    [ATT_CPU_MM_INT]           = {REG_RESET_SIGNAL      , MASK_0001_0000, DATA_HEX},
    [ATT_BIOS_MUX_SEL]         = {REG_BIOS_MUX_SEL      , MASK_1000_0000, DATA_HEX},
    [ATT_WDT_DOG_ENABLE]       = {REG_WATCHDOG_ENABLE   , MASK_0000_0010, DATA_HEX},
    [ATT_WDT_RCVRY_ENABLE]     = {REG_WATCHDOG_ENABLE   , MASK_0000_0001, DATA_HEX},
    [ATT_BMC_CPU_INT_MASK]     = {REG_MASK              , MASK_0100_0000, DATA_HEX},
    [ATT_CPLD_PCIE_RST_DISABLE]= {REG_MASK              , MASK_0000_0001, DATA_HEX},


    //BMC mailbox
    [ATT_TEMP_MAC_HWM]        = {REG_TEMP_MAC_HWM , MASK_ALL      , DATA_S_DEC},

    //BSP
    [ATT_BSP_VERSION]         = {REG_NONE         , MASK_NONE     , DATA_UNK},
    [ATT_BSP_DEBUG]           = {REG_NONE         , MASK_NONE     , DATA_UNK},
    [ATT_BSP_PR_INFO]         = {REG_NONE         , MASK_NONE     , DATA_UNK},
    [ATT_BSP_PR_ERR]          = {REG_NONE         , MASK_NONE     , DATA_UNK},
    [ATT_BSP_REG]             = {REG_NONE         , MASK_NONE     , DATA_UNK},
    [ATT_BSP_REG_VALUE]       = {REG_NONE         , MASK_NONE     , DATA_HEX},
    [ATT_BSP_GPIO_MAX]        = {REG_NONE         , MASK_NONE     , DATA_DEC},
};

enum bsp_log_types {
    LOG_NONE,
    LOG_RW,
    LOG_READ,
    LOG_WRITE,
    LOG_SYS
};

enum bsp_log_ctrl {
    LOG_DISABLE,
    LOG_ENABLE
};

struct lpc_data_s {
    struct mutex    access_lock;
};

struct lpc_data_s *lpc_data;
char bsp_version[16]="";
char bsp_debug[2]="0";
char bsp_reg[8]="0x0";
u8 enable_log_read  = LOG_DISABLE;
u8 enable_log_write = LOG_DISABLE;
u8 enable_log_sys   = LOG_ENABLE;
u8 mailbox_inited=0;

/* reg shift */
static u8 _shift(u8 mask)
{
    int i=0, mask_one=1;

    for(i=0; i<8; ++i) {
        if ((mask & mask_one) == 1)
            return i;
        else
            mask >>= 1;
    }

    return -1;
}

/* reg mask and shift */
static u8 _mask_shift(u8 val, u8 mask)
{
    int shift=0;

    shift = _shift(mask);

    return (val & mask) >> shift;
}

static u8 _bit_operation(u8 reg_val, u8 bit, u8 bit_val)
{
    if (bit_val == 0)
        reg_val = reg_val & ~(1 << bit);
    else
        reg_val = reg_val | (1 << bit);
    return reg_val;
}

static u8 _parse_data(char *buf, unsigned int data, u8 data_type)
{
    if(buf == NULL) {
        return -1;
    }

    if(data_type == DATA_HEX) {
        return sprintf(buf, "0x%02x", data);
    } else if(data_type == DATA_DEC) {
        return sprintf(buf, "%u", data);
    } else {
        return -1;
    }
    return 0;
}

static int _bsp_log(u8 log_type, char *fmt, ...)
{
    if ((log_type==LOG_READ  && enable_log_read) ||
        (log_type==LOG_WRITE && enable_log_write) ||
        (log_type==LOG_SYS && enable_log_sys) ) {
        va_list args;
        int r;

        va_start(args, fmt);
        r = vprintk(fmt, args);
        va_end(args);

        return r;
    } else {
        return 0;
    }
}

static int _bsp_log_config(u8 log_type)
{
    switch(log_type) {
        case LOG_NONE:
            enable_log_read = LOG_DISABLE;
            enable_log_write = LOG_DISABLE;
            break;
        case LOG_RW:
            enable_log_read = LOG_ENABLE;
            enable_log_write = LOG_ENABLE;
            break;
        case LOG_READ:
            enable_log_read = LOG_ENABLE;
            enable_log_write = LOG_DISABLE;
            break;
        case LOG_WRITE:
            enable_log_read = LOG_DISABLE;
            enable_log_write = LOG_ENABLE;
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static void _outb(u8 data, u16 port)
{
    outb(data, port);
    mdelay(LPC_MDELAY);
}

/* init bmc mailbox, get from BMC team */
static int bmc_mailbox_init(void)
{
    if (mailbox_inited) {
        return mailbox_inited;
    }

    //Enable super io writing
    _outb(0xa5, 0x2e);
    _outb(0xa5, 0x2e);

    //Logic device number
    _outb(0x07, 0x2e);
    _outb(0x0e, 0x2f);

    //Disable mailbox
    _outb(0x30, 0x2e);
    _outb(0x00, 0x2f);

    //Set base address bit
    _outb(0x60, 0x2e);
    _outb(0x07, 0x2f);
    _outb(0x61, 0x2e);
    _outb(0xc0, 0x2f);

    //Select bit[3:0] of SIRQ
    _outb(0x70, 0x2e);
    _outb(0x07, 0x2f);

    //Low level trigger
    _outb(0x71, 0x2e);
    _outb(0x01, 0x2f);

    //Enable mailbox
    _outb(0x30, 0x2e);
    _outb(0x01, 0x2f);

    //Disable super io writing
    _outb(0xaa, 0x2e);

    //Mailbox initial
    _outb(0x00, 0x786);
    _outb(0x00, 0x787);

    //set mailbox_inited
    mailbox_inited = 1;

    return mailbox_inited;
}

/* get lpc register value */
static u8 _lpc_reg_read(u16 reg, u8 mask)
{
    u8 reg_val=0x0, reg_mk_shf_val = 0x0;

    mutex_lock(&lpc_data->access_lock);
    reg_val = inb(reg);
    mutex_unlock(&lpc_data->access_lock);

    reg_mk_shf_val = _mask_shift(reg_val, mask);

    BSP_LOG_R("reg=0x%03x, reg_val=0x%02x, mask=0x%02x, reg_mk_shf_val=0x%02x", reg, reg_val, mask, reg_mk_shf_val);

    return reg_mk_shf_val;
}

/* get lpc register value */
static ssize_t lpc_reg_read(u16 reg, u8 mask, char *buf, u8 data_type)
{
    u8 reg_val;
    int len=0;

    reg_val = _lpc_reg_read(reg, mask);

    // may need to change to hex value ?
    len=_parse_data(buf, reg_val, data_type);

    return len;
}

/* set lpc register value */
static ssize_t lpc_reg_write(u16 reg, u8 mask, const char *buf, size_t count, u8 data_type)
{
    u8 reg_val, reg_val_now, shift;

    if (kstrtou8(buf, 0, &reg_val) < 0) {
        if(data_type == DATA_S_DEC) {
            if (kstrtos8(buf, 0, &reg_val) < 0) {
                return -EINVAL;
            }
        } else {
            return -EINVAL;
        }
    }

    //apply continuous bits operation if mask is specified, discontinuous bits are not supported
    if (mask != MASK_ALL) {
        reg_val_now = _lpc_reg_read(reg, MASK_ALL);
        //clear bits in reg_val_now by the mask
        reg_val_now &= ~mask;
        //get bit shift by the mask
        shift = _shift(mask);
        //calculate new reg_val
        reg_val = _bit_operation(reg_val_now, shift, reg_val);
    }

    mutex_lock(&lpc_data->access_lock);

    _outb(reg_val, reg);

    mutex_unlock(&lpc_data->access_lock);

    BSP_LOG_W("reg=0x%03x, reg_val=0x%02x, mask=0x%02x", reg, reg_val, mask);

    return count;
}

/* get bsp value */
static ssize_t bsp_read(char *buf, char *str)
{
    ssize_t len=0;

    mutex_lock(&lpc_data->access_lock);
    len=sprintf(buf, "%s", str);
    mutex_unlock(&lpc_data->access_lock);

    BSP_LOG_R("reg_val=%s", str);

    return len;
}

/* set bsp value */
static ssize_t bsp_write(const char *buf, char *str, size_t str_len, size_t count)
{
    mutex_lock(&lpc_data->access_lock);
    snprintf(str, str_len, "%s", buf);
    mutex_unlock(&lpc_data->access_lock);

    BSP_LOG_W("reg_val=%s", str);

    return count;
}

/* get gpio max value */
static ssize_t gpio_max_show(struct device *dev,
                    struct device_attribute *da,
                    char *buf)
{
    u8 data_type=DATA_UNK;
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    if (attr->index == ATT_BSP_GPIO_MAX) {
        data_type = attr_reg[attr->index].data_type;
        return _parse_data(buf, ARCH_NR_GPIOS-1, data_type);
    }
    return -1;
}


/* get lpc register value */
static ssize_t lpc_callback_show(struct device *dev,
        struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    u16 reg = 0;
    u8 mask = MASK_NONE;
    u8 data_type=DATA_UNK;

    switch (attr->index) {
        // CPU CPLD
        case ATT_CPU_CPLD_VERSION:
        case ATT_BMC_CPU_NMI:
        case ATT_BMC_CPU_INT:
        case ATT_CPU_MB_RST:
        case ATT_CPU_BMC_RST:
        case ATT_CPU_MM_INT:
        case ATT_BIOS_MUX_SEL:
        case ATT_WDT_DOG_ENABLE:
        case ATT_WDT_RCVRY_ENABLE:
        case ATT_BMC_CPU_INT_MASK:
        case ATT_CPLD_PCIE_RST_DISABLE:

        //BSP
        case ATT_BSP_GPIO_MAX:
            reg = attr_reg[attr->index].reg;
            mask= attr_reg[attr->index].mask;
            data_type = attr_reg[attr->index].data_type;
            break;
        case ATT_BSP_REG_VALUE:
            if (kstrtou16(bsp_reg, 0, &reg) < 0)
                return -EINVAL;

            mask = MASK_ALL;
            break;
        default:
            return -EINVAL;
    }
    return lpc_reg_read(reg, mask, buf, data_type);
}

/* set lpc register value */
static ssize_t lpc_callback_store(struct device *dev,
        struct device_attribute *da, const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    u16 reg = 0;
    u8 mask = MASK_NONE;
    u8 data_type=DATA_UNK;

    switch (attr->index) {

        //BMC mailbox
        case ATT_TEMP_MAC_HWM:
            reg = attr_reg[attr->index].reg;
            mask= attr_reg[attr->index].mask;
            data_type = attr_reg[attr->index].data_type;
            break;
        default:
            return -EINVAL;
    }

    if(attr->index == ATT_TEMP_MAC_HWM) {
            bmc_mailbox_init();
    }

    return lpc_reg_write(reg, mask, buf, count, data_type);

}

/* get bsp parameter value */
static ssize_t bsp_callback_show(struct device *dev,
        struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    char *str=NULL;

    switch (attr->index) {
        case ATT_BSP_VERSION:
            str = bsp_version;
            break;
        case ATT_BSP_DEBUG:
            str = bsp_debug;
            break;
        case ATT_BSP_REG:
            str = bsp_reg;
            break;
        default:
            return -EINVAL;
    }
    return bsp_read(buf, str);
}

/* set bsp parameter value */
static ssize_t bsp_callback_store(struct device *dev,
        struct device_attribute *da, const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int str_len=0;
    char *str=NULL;
    u16 reg = 0;
    u8 bsp_debug_u8 = 0;

    switch (attr->index) {
        case ATT_BSP_VERSION:
            str = bsp_version;
            str_len = sizeof(bsp_version);
            break;
        case ATT_BSP_DEBUG:
            if (kstrtou8(buf, 0, &bsp_debug_u8) < 0) {
                return -EINVAL;
            } else if (_bsp_log_config(bsp_debug_u8) < 0) {
                return -EINVAL;
            }

            str = bsp_debug;
            str_len = sizeof(bsp_debug);
            break;
        case ATT_BSP_REG:
            if (kstrtou16(buf, 0, &reg) < 0)
                return -EINVAL;

            str = bsp_reg;
            str_len = sizeof(bsp_reg);
            break;
        default:
            return -EINVAL;
    }

    return bsp_write(buf, str, str_len, count);
}

static ssize_t bsp_pr_callback_store(struct device *dev,
        struct device_attribute *da, const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int str_len = strlen(buf);

    if(str_len <= 0)
        return str_len;

    switch (attr->index) {
        case ATT_BSP_PR_INFO:
            BSP_PR(KERN_INFO, "%s", buf);
            break;
        case ATT_BSP_PR_ERR:
            BSP_PR(KERN_ERR, "%s", buf);
            break;
        default:
            return -EINVAL;
    }

    return str_len;
}

//SENSOR_DEVICE_ATTR - CPU_CPLD
static SENSOR_DEVICE_ATTR_RO(cpu_cpld_version         , lpc_callback     , ATT_CPU_CPLD_VERSION);
static SENSOR_DEVICE_ATTR_RW(bmc_cpu_nmi              , lpc_callback     , ATT_BMC_CPU_NMI);
static SENSOR_DEVICE_ATTR_RW(bmc_cpu_interrupt        , lpc_callback     , ATT_BMC_CPU_INT);
static SENSOR_DEVICE_ATTR_RW(cpu_mb_rst               , lpc_callback     , ATT_CPU_MB_RST);
static SENSOR_DEVICE_ATTR_RW(cpu_bmc_rst              , lpc_callback     , ATT_CPU_BMC_RST);
static SENSOR_DEVICE_ATTR_RW(cpu_mm_interrupt         , lpc_callback     , ATT_CPU_MM_INT);
static SENSOR_DEVICE_ATTR_RO(bios_mux_sel             , lpc_callback     , ATT_BIOS_MUX_SEL);
static SENSOR_DEVICE_ATTR_RW(wdt_dog_enable           , lpc_callback     , ATT_WDT_DOG_ENABLE);
static SENSOR_DEVICE_ATTR_RW(wdt_rcvry_enable         , lpc_callback     , ATT_WDT_RCVRY_ENABLE);
static SENSOR_DEVICE_ATTR_RW(bmc_cpu_interrupt_mask   , lpc_callback     , ATT_BMC_CPU_INT_MASK);
static SENSOR_DEVICE_ATTR_RW(cpld_pcie_rst_disable    , lpc_callback     , ATT_CPLD_PCIE_RST_DISABLE);

//SENSOR_DEVICE_ATTR - BMC mailbox
static SENSOR_DEVICE_ATTR_WO(temp_mac_hwm        , lpc_callback     , ATT_TEMP_MAC_HWM);

//SENSOR_DEVICE_ATTR - BSP
static SENSOR_DEVICE_ATTR_RW(bsp_version         , bsp_callback     , ATT_BSP_VERSION);
static SENSOR_DEVICE_ATTR_RW(bsp_debug           , bsp_callback     , ATT_BSP_DEBUG);
static SENSOR_DEVICE_ATTR_WO(bsp_pr_info         , bsp_pr_callback  , ATT_BSP_PR_INFO);
static SENSOR_DEVICE_ATTR_WO(bsp_pr_err          , bsp_pr_callback  , ATT_BSP_PR_ERR);
static SENSOR_DEVICE_ATTR_RW(bsp_reg             , bsp_callback     , ATT_BSP_REG);
static SENSOR_DEVICE_ATTR_RO(bsp_reg_value       , lpc_callback     , ATT_BSP_REG_VALUE);
static SENSOR_DEVICE_ATTR_RO(bsp_gpio_max        , gpio_max         , ATT_BSP_GPIO_MAX);

static struct attribute *cpu_cpld_attrs[] = {
    _DEVICE_ATTR(cpu_cpld_version),
    _DEVICE_ATTR(bmc_cpu_nmi),
    _DEVICE_ATTR(bmc_cpu_interrupt),
    _DEVICE_ATTR(cpu_mb_rst),
    _DEVICE_ATTR(cpu_bmc_rst),
    _DEVICE_ATTR(cpu_mm_interrupt),
    _DEVICE_ATTR(bios_mux_sel),
    _DEVICE_ATTR(wdt_dog_enable),
    _DEVICE_ATTR(wdt_rcvry_enable),
    _DEVICE_ATTR(bmc_cpu_interrupt_mask),
    _DEVICE_ATTR(cpld_pcie_rst_disable),
    NULL,
};

static struct attribute *bmc_mailbox_attrs[] = {
    _DEVICE_ATTR(temp_mac_hwm),
    NULL,
};

static struct attribute *bsp_attrs[] = {
    _DEVICE_ATTR(bsp_version),
    _DEVICE_ATTR(bsp_debug),
    _DEVICE_ATTR(bsp_pr_info),
    _DEVICE_ATTR(bsp_pr_err),
    _DEVICE_ATTR(bsp_reg),
    _DEVICE_ATTR(bsp_reg_value),
    _DEVICE_ATTR(bsp_gpio_max),
    NULL,
};

static struct attribute_group cpu_cpld_attr_grp = {
    .name = "cpu_cpld",
    .attrs = cpu_cpld_attrs,
};

static struct attribute_group bsp_attr_grp = {
    .name = "bsp",
    .attrs = bsp_attrs,
};

static struct attribute_group bmc_mailbox_attr_grp = {
    .name = "bmc_mailbox",
    .attrs = bmc_mailbox_attrs,
};

static void lpc_dev_release( struct device * dev)
{
    return;
}

static struct platform_device lpc_dev = {
    .name           = DRIVER_NAME,
    .id             = -1,
    .dev = {
                    .release = lpc_dev_release,
    }
};

static int lpc_drv_probe(struct platform_device *pdev)
{
    int i = 0, grp_num = 3;
    int err[3] = {0};
    struct attribute_group *grp;

    lpc_data = devm_kzalloc(&pdev->dev, sizeof(struct lpc_data_s),
                    GFP_KERNEL);
    if (!lpc_data)
        return -ENOMEM;

    mutex_init(&lpc_data->access_lock);

    for (i=0; i<grp_num; ++i) {
        switch (i) {
            case 0:
                grp = &cpu_cpld_attr_grp;
                break;
            case 1:
                grp = &bsp_attr_grp;
                break;
            case 2:
                grp = &bmc_mailbox_attr_grp;
                break;
            default:
                break;
        }

        err[i] = sysfs_create_group(&pdev->dev.kobj, grp);
        if (err[i]) {
            printk(KERN_ERR "Cannot create sysfs for group %s\n", grp->name);
            goto exit;
        } else {
            continue;
        }
    }

    return 0;

exit:
    for (i=0; i<grp_num; ++i) {
        switch (i) {
            case 0:
                grp = &cpu_cpld_attr_grp;
                break;
            case 1:
                grp = &bsp_attr_grp;
                break;
            case 2:
                grp = &bmc_mailbox_attr_grp;
                break;
            default:
                break;
        }

        sysfs_remove_group(&pdev->dev.kobj, grp);
        if (!err[i]) {
            //remove previous successful cases
            continue;
        } else {
            //remove first failed case, then return
            return err[i];
        }
    }
    return 0;
}

static int lpc_drv_remove(struct platform_device *pdev)
{
    sysfs_remove_group(&pdev->dev.kobj, &cpu_cpld_attr_grp);
    sysfs_remove_group(&pdev->dev.kobj, &bsp_attr_grp);
    sysfs_remove_group(&pdev->dev.kobj, &bmc_mailbox_attr_grp);
    return 0;
}

static struct platform_driver lpc_drv = {
    .probe  = lpc_drv_probe,
    .remove = __exit_p(lpc_drv_remove),
    .driver = {
    .name   = DRIVER_NAME,
    },
};

int lpc_init(void)
{
    int err = 0;

    err = platform_driver_register(&lpc_drv);
    if (err) {
    	printk(KERN_ERR "%s(#%d): platform_driver_register failed(%d)\n",
                __func__, __LINE__, err);

    	return err;
    }

    err = platform_device_register(&lpc_dev);
    if (err) {
    	printk(KERN_ERR "%s(#%d): platform_device_register failed(%d)\n",
                __func__, __LINE__, err);
    	platform_driver_unregister(&lpc_drv);
    	return err;
    }

    return err;
}

void lpc_exit(void)
{
    platform_driver_unregister(&lpc_drv);
    platform_device_unregister(&lpc_dev);
}

MODULE_AUTHOR("Melo Lin <melo.lin@ufispace.com>");
MODULE_DESCRIPTION("x86_64_ufispace_s9180_32x_lpc driver");
MODULE_LICENSE("GPL");

module_init(lpc_init);
module_exit(lpc_exit);
