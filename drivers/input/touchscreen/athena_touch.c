/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR
 *    1.0		  2010-01-05			WenFS
 *
 * note: only support mulititouch	Wenfs 2010-10-01
 */



#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x06_ts.h"
#include <linux/input/ft5x06_ts.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <mach/vreg.h>
#include <linux/file.h>
#include <mach/irqs.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
//#include <asm/jzsoc.h>

//extern int is_usb_pluge(void);
//static int open_emi_on = 0;
int release_flag = 0;
static int ctp_debug_info_enable = 1;
unsigned char FW_Ver=-1;
#define ctp_debug_info(fmt, ...)	do{if(ctp_debug_info_enable){printk(KERN_DEBUG "focaltech: "fmt, ##__VA_ARGS__);} }while(0)	
#define CFG_SUPPORT_AUTO_UPG 1
static struct i2c_client *this_client;
//static struct ft5x0x_ts_platform_data *pdata;
#if 1
static int start = 0;	//start down fw for CTP 1:enable
#endif
#define CONFIG_FT5X0X_MULTITOUCH 1
#define POLLING		1
#define INT_TRIGGER	2
#define SYS_DEBUG 1
#if SYS_DEBUG
static struct mutex g_device_mutex;
unsigned char W27_VENDOR_ID=-1; 
static int W27_CTP_TYPE = INT_TRIGGER;
unsigned char *buf;
#endif
//long fsize;
int slip_flag = 0;
struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
	u8  touch_point;
};

struct ft5x0x_ts_data {
	struct input_dev	*input_dev;
	//[--WNC Ophas 2011-08-18, for gesture key of touch screen B area
	struct input_dev	*input_key_dev;
	//WNC Ophas 2011-08-18, for gesture key of touch screen B area--]
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
	int report_flag;
	uint16_t keycode;
};

static struct vreg *vreg_touch;  

unsigned char FTS_CTPM_GetVendorID_Info(void);

#if 0
extern struct input_dev *keypad_dev; 
static struct input_dev *kpdevice;
struct input_dev *msm_keypad_get_input_dev_adapt(void)
{
	ctp_debug_info("msm_keypad_get_input_dev_adapt()...\n");
	return keypad_dev;
}
#endif

/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
 *length

Output	:	ret

function	:	

 ***********************************************************************************************/
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
	{
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
		gpio_set_value(FT5x06_WAKEUP, 0);
		mdelay(1);
		gpio_set_value(FT5x06_WAKEUP, 1);
		mdelay(300);
		
	}

	return ret;
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
	{
		pr_err("%s i2c write error: %d\n", __func__, ret);
		gpio_set_value(FT5x06_WAKEUP, 0);
		mdelay(1);
		gpio_set_value(FT5x06_WAKEUP, 1);
		mdelay(300);
		
	}

	return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_write_reg

Input	:	addr -- address
para -- parameter

Output	:	

function	:	write register of ft5x0x

 ***********************************************************************************************/
static int ft5x0x_write_reg(u8 addr, u8 para)
{
	u8 buf[3];
	int ret = -1;

	buf[0] = addr;
	buf[1] = para;
	ret = ft5x0x_i2c_txdata(buf, 2);
	if (ret < 0) {
		pr_err("write reg failed! %#x ret: %d", buf[0], ret);
		return -1;
	}

	return 0;
}


/***********************************************************************************************
Name	:	ft5x0x_read_reg 

Input	:	addr
pdata

Output	:	

function	:	read register of ft5x0x

 ***********************************************************************************************/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2] = {0};

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= buf,
		},
	};

	buf[0] = addr;

	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
	{
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
		gpio_set_value(FT5x06_WAKEUP, 0);
		mdelay(1);
		gpio_set_value(FT5x06_WAKEUP, 1);
		mdelay(300);
		
	}


	*pdata = buf[0];
	return ret;

}


/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void


Output	:	 firmware version 	

function	:	 read TP firmware version

 ***********************************************************************************************/
static unsigned char ft5x0x_read_fw_ver(void)
{
	unsigned char ver = 0;
	ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
	return(ver);
}

static unsigned char read_vendor_ID(void)
{
	unsigned char id = 0;
	ft5x0x_read_reg(FT5X0X_REG_FT5201ID, &id);
	return(id);
}

#define CONFIG_SUPPORT_FTS_CTP_UPG


#ifdef CONFIG_SUPPORT_FTS_CTP_UPG


typedef enum
{
	ERR_OK,
	ERR_MODE,
	ERR_READID,
	ERR_ERASE,
	ERR_STATUS,
	ERR_ECC,
	ERR_DL_ERASE_FAIL,
	ERR_DL_PROGRAM_FAIL,
	ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit

#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE              0x0

#define I2C_CTPM_ADDRESS       0x70


void delay_qt_ms(unsigned long  w_ms)
{
	unsigned long i;
	unsigned long j;

	for (i = 0; i < w_ms; i++)
	{
		for (j = 0; j < 1000; j++)
		{
			udelay(1);
		}
	}
}


/*
   [function]: 
callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
bt_ctpm_addr[in]    :the address of the ctpm;
pbt_buf[out]        :data buffer;
dw_lenth[in]        :the length of the data buffer;
[return]:
FTS_TRUE     :success;
FTS_FALSE    :fail;
*/
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	int ret;

	ret=i2c_master_recv(this_client, pbt_buf, dw_lenth);

	if(ret<=0)
	{
		ctp_debug_info("[TSP]i2c_read_interface error\n");
		return FTS_FALSE;
	}

	return FTS_TRUE;
}

/*
   [function]: 
callback: write data to ctpm by i2c interface,implemented by special user;
[parameters]:
bt_ctpm_addr[in]    :the address of the ctpm;
pbt_buf[in]        :data buffer;
dw_lenth[in]        :the length of the data buffer;
[return]:
FTS_TRUE     :success;
FTS_FALSE    :fail;
*/
FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	int ret;
	ret=i2c_master_send(this_client, pbt_buf, dw_lenth);
	if(ret<=0)
	{
		ctp_debug_info("[TSP]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
		return FTS_FALSE;
	}

	return FTS_TRUE;
}

/*
   [function]: 
   send a command to ctpm.
   [parameters]:
   btcmd[in]        :command code;
   btPara1[in]    :parameter 1;    
   btPara2[in]    :parameter 2;    
   btPara3[in]    :parameter 3;    
   num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
	FTS_BYTE write_cmd[4] = {0};

	write_cmd[0] = btcmd;
	write_cmd[1] = btPara1;
	write_cmd[2] = btPara2;
	write_cmd[3] = btPara3;
	return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
   [function]: 
   write data to ctpm , the destination address is 0.
   [parameters]:
   pbt_buf[in]    :point to data buffer;
   bt_len[in]        :the data numbers;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{

	return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

/*
   [function]: 
   read out data from ctpm,the destination address is 0.
   [parameters]:
   pbt_buf[out]    :point to data buffer;
   bt_len[in]        :the data numbers;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
	return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


/*
   [function]: 
   burn the FW to ctpm.
   [parameters]:(ref. SPEC)
   pbt_buf[in]    :point to Head+FW ;
   dw_lenth[in]:the length of the FW + 6(the Head length);    
   bt_ecc[in]    :the ECC of the FW
   [return]:
ERR_OK        :no error;
ERR_MODE    :fail to switch to UPDATE mode;
ERR_READID    :read id fail;
ERR_ERASE    :erase chip fail;
ERR_STATUS    :status error;
ERR_ECC        :ecc error.
*/

#define    FTS_SETTING_BUF_LEN      128
#define    FTS_PACKET_LENGTH        128

static unsigned char CTPM_FW_OFILM[]=
{
//#include "sicily_ofilm_app.i"
};

static unsigned char CTPM_FW_MUDONG[]=
{
//#include "sicily_mudong_app.i"
};

static unsigned char CTPM_FW_TOPTOUCH[]=
{
#include "athena_toptouch_app.i"
};

E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;

    FTS_DWRD  packet_number;
    FTS_DWRD  j;
    FTS_DWRD  temp;
    FTS_DWRD  lenght;
    FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE bt_ecc;
    int      i_ret;
    unsigned char au_delay_timing[11] = {30, 33, 36, 39, 42, 45, 27, 24,21,18,15};
    unsigned char retry = 0;


UPG_RESTART:
    /*********Step 1:Reset  CTPM *****/
    /*write 0xaa to register 0xfc*/
    ft5x0x_write_reg(0xfc,0xaa);
    delay_qt_ms(50);
     /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");
   
    delay_qt_ms(au_delay_timing[retry]);   


    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        delay_qt_ms(5);
    }while(i_ret <= 0 && i < 5 );

    /*********Step 3:check READ-ID***********************/        
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == 0x7)
    {
        printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
	if(retry < 10){
		retry++;
		msleep(200);
		printk(KERN_DEBUG"[FTS]Step 3:Reset failed retry: %d", retry);
		goto UPG_RESTART;
	}

        return ERR_READID;
        //i_is_new_protocol = 1;
    }

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] bootloader version = 0x%x\n", reg_val[0]);

     /*********Step 4:erase app and panel paramenter area ********************/
    cmd_write(0x61,0x00,0x00,0x00,1);  //erase app area
    delay_qt_ms(1500); 
    cmd_write(0x63,0x00,0x00,0x00,1);  //erase panel parameter area
    delay_qt_ms(100);
    printk("[FTS] Step 4: erase. \n");

    /*********Step 5:write firmware(FW) to ctpm flash*********/
    bt_ecc = 0;
    printk("[FTS] Step 5: start upgrade. \n");
    dw_lenth = dw_lenth - 8;
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(lenght>>8);
        packet_buf[5] = (FTS_BYTE)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }
        
        byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
        delay_qt_ms(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
              printk("[FTS] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],temp+6);    
        delay_qt_ms(20);
    }

    //send the last six byte
    for (i = 0; i<6; i++)
    {
        temp = 0x6ffa + i;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        temp =1;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;
        packet_buf[6] = pbt_buf[ dw_lenth + i]; 
        bt_ecc ^= packet_buf[6];

        byte_write(&packet_buf[0],7);  
        delay_qt_ms(20);
    }

    /*********Step 6: read out checksum***********************/
    /*send the opration head*/
    cmd_write(0xcc,0x00,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
    if(reg_val[0] != bt_ecc)
    {
        return ERR_ECC;
    }

    /*********Step 7: reset the new FW***********************/
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(300);  //make sure CTP startup normally
    
    return ERR_OK;
}








#if 0
E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	FTS_BYTE reg_val[2] = {0};
	FTS_DWRD i = 0;

	FTS_DWRD  packet_number;
	FTS_DWRD  j;
	FTS_DWRD  temp;
	FTS_DWRD  lenght;
	FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
	FTS_BYTE  auc_i2c_write_buf[10];
	FTS_BYTE bt_ecc;
	int      i_ret;

	/*********Step 1:Reset  CTPM *****/
	/*write 0xaa to register 0xfc*/
	ft5x0x_write_reg(0xfc,0xaa);
	delay_qt_ms(50);
	/*write 0x55 to register 0xfc*/
	ft5x0x_write_reg(0xfc,0x55);
	printk("[TSP] Step 1: Reset CTPM test\n");

	delay_qt_ms(30);   


	/*********Step 2:Enter upgrade mode *****/
	auc_i2c_write_buf[0] = 0x55;
	auc_i2c_write_buf[1] = 0xaa;
	do
	{
		i ++;
		i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
		delay_qt_ms(5);
	}while(i_ret <= 0 && i < 5 );

	/*********Step 3:check READ-ID***********************/        
	cmd_write(0x90,0x00,0x00,0x00,4);
	byte_read(reg_val,2);
	if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
	{
		printk("[TSP] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	}
	else
	{
		return ERR_READID;
		//i_is_new_protocol = 1;
	}

	/*********Step 4:erase app*******************************/
	cmd_write(0x61,0x00,0x00,0x00,1);

	delay_qt_ms(1500);
	printk("[TSP] Step 4: erase. \n");

	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;
	ctp_debug_info("[TSP] Step 5: start upgrade. \n");
	dw_lenth = dw_lenth - 8;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;
	for (j=0;j<packet_number;j++)
	{
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (FTS_BYTE)(lenght>>8);
		packet_buf[5] = (FTS_BYTE)lenght;

		for (i=0;i<FTS_PACKET_LENGTH;i++)
		{
			packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
			bt_ecc ^= packet_buf[6+i];
		}

		byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
		delay_qt_ms(FTS_PACKET_LENGTH/6 + 1);
		if ((j * FTS_PACKET_LENGTH % 1024) == 0)
		{
			printk("[TSP] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
		}
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
	{
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;

		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (FTS_BYTE)(temp>>8);
		packet_buf[5] = (FTS_BYTE)temp;

		for (i=0;i<temp;i++)
		{
			packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
			bt_ecc ^= packet_buf[6+i];
		}

		byte_write(&packet_buf[0],temp+6);    
		delay_qt_ms(20);
	}

	//send the last six byte
	for (i = 0; i<6; i++)
	{
		temp = 0x6ffa + i;
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;
		temp =1;
		packet_buf[4] = (FTS_BYTE)(temp>>8);
		packet_buf[5] = (FTS_BYTE)temp;
		packet_buf[6] = pbt_buf[ dw_lenth + i]; 
		bt_ecc ^= packet_buf[6];

		byte_write(&packet_buf[0],7);  
		delay_qt_ms(20);
	}

	/*********Step 6: read out checksum***********************/
	/*send the opration head*/
	cmd_write(0xcc,0x00,0x00,0x00,1);
	byte_read(reg_val,1);
	printk("[TSP] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
	if(reg_val[0] != bt_ecc)
	{
		return ERR_ECC;
	}

	/*********Step 7: reset the new FW***********************/
	cmd_write(0x07,0x00,0x00,0x00,1);

	return ERR_OK;
}
#endif
int fts_ctpm_auto_clb(void)
{
	unsigned char uc_temp;
	unsigned char i;

	printk("[FTS] start atuo CLB.\n");
	msleep(200);
	ft5x0x_write_reg(0, 0x40);
	delay_qt_ms(100);
	ft5x0x_write_reg(2, 0x4);
	delay_qt_ms(300);
	for(i = 0; i < 100; i++)
	{
		ft5x0x_read_reg(0, &uc_temp);
		if ( ((uc_temp & 0x70) >> 4) == 0x0)
		{
			break;
		}
		delay_qt_ms(200);
		printk("[FTS] waiting calibration %d\n", i);
	
    }
    printk("[FTS] calibration OK.\n");
    
    msleep(300);
    ft5x0x_write_reg(0, 0x40);  //goto factory mode
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x5);  //store CLB result
    delay_qt_ms(300);
    ft5x0x_write_reg(0, 0x0); //return to normal mode 
    msleep(300);
    printk("[FTS] store CLB result OK.\n");
    return 0;
}

int fts_ctpm_fw_upgrade_with_i_file(unsigned char vendor_id)
{
	FTS_BYTE*	pbt_buf = FTS_NULL;
	int i_ret;

	if (vendor_id == 0x51){
		pbt_buf = CTPM_FW_OFILM;
		i_ret = fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW_OFILM));
	}
	else if(vendor_id == 0x53){
		pbt_buf = CTPM_FW_MUDONG;
		i_ret = fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW_MUDONG));
	}else if (vendor_id == 0xa0)
	{

		pbt_buf = CTPM_FW_TOPTOUCH;
		i_ret = fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW_TOPTOUCH));
	}
	else
		return 1;


//	pbt_buf = CTPM_FW;

//	i_ret = fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));
	if (i_ret != 0)
	{
		printk("[FTS] upgrade failed i_ret = %d.\n", i_ret);
	}else
	{printk("[FTS] upgrade successfully.\n");
		fts_ctpm_auto_clb();
	}
	return i_ret;
}

unsigned char fts_ctpm_get_i_file_ver(void)
{
    unsigned int ui_sz;
    unsigned char uc_tp_vendor_id = read_vendor_ID();
    if(uc_tp_vendor_id == 0xa8 || uc_tp_vendor_id == 0)
	uc_tp_vendor_id = FTS_CTPM_GetVendorID_Info();
    ui_sz = sizeof(CTPM_FW_OFILM);
	if(uc_tp_vendor_id == 0x51){	
    ui_sz = sizeof(CTPM_FW_OFILM);
    if (ui_sz > 2)
    {
		printk("ofilm i file version %x\n",CTPM_FW_OFILM[ui_sz -2]);
	    return CTPM_FW_OFILM[ui_sz - 2];
    }
    else
    {

	    return 0xff;
    }
	}else if(uc_tp_vendor_id == 0x53){
    ui_sz = sizeof(CTPM_FW_MUDONG);
    if (ui_sz > 2)
    {
		printk("mudong i file version %x\n",CTPM_FW_MUDONG[ui_sz -2]);
	    return CTPM_FW_MUDONG[ui_sz - 2];
    }
    else
    {

	    return 0xff;
    }

	}else if(uc_tp_vendor_id ==0xa0){
    ui_sz = sizeof(CTPM_FW_TOPTOUCH);

    if (ui_sz > 2)
    {
		printk("TOPTOUCH i file version %x\n",CTPM_FW_TOPTOUCH[ui_sz -2]);
	//	printk("TOPTOUCH i file version \n");
	    return CTPM_FW_TOPTOUCH[ui_sz - 2];
    }
    else
    {

	    return 0xff;
    }
	}else
	{
		printk("vendor id is unknown\n");
		return 0xff;
	}		
}
#if 0
int fts_ctpm_fw_upgrade_with_i_file(void)
{
	FTS_BYTE*     pbt_buf = FTS_NULL;
	int i_ret;

	//=========FW upgrade========================*/
	pbt_buf = buf;
	/*call the upgrade function*/
	i_ret =  fts_ctpm_fw_upgrade(pbt_buf, fsize);
	if (i_ret != 0)
	{
		//error handling ...
		//TBD
	}

	return i_ret;
}
#endif
/*
   unsigned char fts_ctpm_get_upg_ver(void)
   {
   unsigned int ui_sz;
   ui_sz = fsize;
   if (ui_sz > 2)
   {
   return CTPM_FW[ui_sz - 2];
   }
   else
   {
//TBD, error handling?
return 0xff; //default value
}
}
*/

unsigned char FTS_CTPM_GetVendorID_Info(void)
{
	unsigned char buf[FTS_SETTING_BUF_LEN];
	unsigned char reg_val[2] = {0};
	unsigned char auc_i2c_write_buf[10];
//	unsigned char packet_buf[FTS_SETTING_BUF_LEN + 6];
	unsigned int  i = 0;
	int      i_ret;
	unsigned char au_delay_timing[11] = {30, 33, 36, 39, 42, 45, 27, 24,21,18,15};
	unsigned char j = 0;
	 UPG_START:
	/*********Step 1:Reset  CTPM *****/
	/*write 0xaa to register 0xfc*/
	ft5x0x_write_reg(0xfc,0xaa);
	delay_qt_ms(50);
	 /*write 0x55 to register 0xfc*/
	ft5x0x_write_reg(0xfc,0x55);
	printk("[FTS] Step 1: GetVendorID Reset CTPM test\n");

	delay_qt_ms(au_delay_timing[j]);

	/*********Step 2:Enter upgrade mode *****/
	auc_i2c_write_buf[0] = 0x55;
	auc_i2c_write_buf[1] = 0xaa;
	do
	{
		     i ++;
		   //  i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
		     i_ret = cmd_write(0x55, 0xaa, 0x00, 0x00, 2);
		     delay_qt_ms(5);
	}while(i_ret <= 0 && i < 5 );
	 
	/*********Step 3:check READ-ID***********************/
	auc_i2c_write_buf[0] = 0x90;
	auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;
//	ft5x0x_i2c_rxdata(auc_i2c_write_buf, 4, reg_val, 2);
	cmd_write(0x90, 0x00, 0x00, 0x00, 4);
	byte_read(reg_val, 2);
	if (reg_val[0] == 0x79 && reg_val[1] == 0x7)
	{
		     printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	}
	else
	{
		if (j < 10)
		{
			j ++;
			msleep(200);
			goto UPG_START;
		}
		else
		{
			return ERR_READID;
		}
	}
	auc_i2c_write_buf[0] = 0xcd;
//	ft5x0x_i2c_rxdata(auc_i2c_write_buf, 1, reg_val, 1);
	cmd_write(0xcd, 0x00, 0x00, 0x00, 1);
	byte_read(reg_val, 1);
	printk("bootloader version = 0x%x\n", reg_val[0]);
	
	/* --------- read current project setting  ---------- */
buf[0] = 0x3;
buf[1] = 0x0;
buf[2] = 0x78;
buf[3] = 0x4;
//ft5x0x_i2c_Write(buf, 4);
//byte_read(buf, FTS_SETTING_BUF_LEN);
//ft5x0x_i2c_rxdata(buf, 4, buf, 1);
	cmd_write(0x3, 0x0, 0x78, 0x4, 4);
	byte_read(buf, 1);
//I2C slave address (8 bit address)
    //IO Voltage 0---3.3v; 1----1.8v
    //TP panel factory ID
    printk("[FTS] old setting: uc_panel_factory_id = 0x%x\n", buf[0]);
        /********* reset the new FW***********************/
    auc_i2c_write_buf[0] = 0x07;
//    ft5x0x_i2c_txdata(auc_i2c_write_buf, 1);
	cmd_write(0x07, 0x00, 0x00, 0x00, 1);
    msleep(300);

    return buf[0];                      //TP panel factory ID
}
#if CFG_SUPPORT_AUTO_UPG

int fts_ctpm_auto_upg(void)
{
//	unsigned char uc_host_fm_ver;
	unsigned char uc_tp_fm_ver;
	unsigned char uc_tp_vendor_id;
	unsigned char uc_host_fm_ver;
	int	      i_ret;

	uc_tp_fm_ver = ft5x0x_read_fw_ver();
	uc_host_fm_ver = fts_ctpm_get_i_file_ver();
	uc_tp_vendor_id = read_vendor_ID();
	if (/*uc_tp_fm_ver == 0xa6 ||*/
			uc_tp_fm_ver < uc_host_fm_ver)

	{
		msleep(100);
		printk("[FTS] uc_tp_fm_ver = 0x%x,uc_host_fm_ver = 0x%x\n",
				uc_tp_fm_ver, uc_host_fm_ver);
		if(uc_tp_vendor_id == 0xa8 || uc_tp_vendor_id == 0)
			uc_tp_vendor_id = FTS_CTPM_GetVendorID_Info();
		i_ret = fts_ctpm_fw_upgrade_with_i_file(uc_tp_vendor_id);
		if (i_ret == 0)
	 	{
			msleep(300);
			uc_host_fm_ver = fts_ctpm_get_i_file_ver();
			printk("[FTS] upgrade to new version 0x%x\n", uc_host_fm_ver);
		}
		else
		{
			printk("[FTS] upgrade failed ret=%d.\n", i_ret);
			printk("%s\n",__func__);
			gpio_set_value(FT5x06_WAKEUP, 0);
			mdelay(1);
			gpio_set_value(FT5x06_WAKEUP, 1);
			msleep(500);
		}
	} 

	return 0;
}

#endif
#endif


s16 up_x1 = 0;
s16 up_y1 = 0;
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_release(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	if(data->report_flag == 1 && !slip_flag){
		input_report_key(data->input_key_dev, data->keycode, 0);
		input_sync(data->input_key_dev);
		data->report_flag = 0;
		printk(KERN_DEBUG"key %d.\n", data->keycode);
		ctp_debug_info("%s\n", __func__);
}else{
#ifdef CONFIG_FT5X0X_MULTITOUCH	
	//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,0);
//	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	//input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
	//input_report_abs(data->input_dev, ABS_MT_POSITION_X, up_x1);
	//input_report_abs(data->input_dev, ABS_MT_POSITION_Y, up_y1);

	//input_report_abs(data->input_dev, ABS_MT_PRESSURE, 0);
//	input_report_key(data->input_dev, BTN_TOUCH, 0);
#else
	input_report_abs(data->input_dev, ABS_PRESSURE, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
#endif
	ctp_debug_info("%s\n", __func__);
}
	slip_flag = 0;
	release_flag = 0;
	input_mt_sync(data->input_dev);
	input_sync(data->input_dev);
}


static int ft5x0x_read_data(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
	u8 buf[32] = {0};
	int ret = -1;

#ifdef CONFIG_FT5X0X_MULTITOUCH
	//	ret = ft5x0x_i2c_rxdata(buf, 13);
	ret = ft5x0x_i2c_rxdata(buf, 31);
#else
	ret = ft5x0x_i2c_rxdata(buf, 7);
#endif
	if (ret < 0) {
		ctp_debug_info("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}

	memset(event, 0, sizeof(struct ts_event));
	//	event->touch_point = buf[2] & 0x03;// 0000 0011
	event->touch_point = buf[2] & 0x07;// 000 0111

	if (event->touch_point == 0) {
		ft5x0x_ts_release();
	}
	else
	{

#ifdef CONFIG_FT5X0X_MULTITOUCH
		switch (event->touch_point) {

			case 5:
				event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
				event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
			case 4:
				event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
				event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
			case 3:
				event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
				event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
			case 2:
				event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
				event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
			case 1:
				event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
				event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
				break;
			default:
				return -1;
		}
#else
		if (event->touch_point == 1) {
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
		}
#endif
		event->pressure = 200;

	}
	return 0;
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_report_value(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
	//u8 uVersion;

	//ctp_debug_info("==ft5x0x_report_value =\n");
#ifdef CONFIG_FT5X0X_MULTITOUCH
	if(event->y1 >860 && !slip_flag){
		switch(event->x1){
			case 80:
				data->keycode = KEY_MENU;
				input_report_key(data->input_key_dev, data->keycode, 1);
				input_sync(data->input_key_dev);
				data->report_flag = 1;
				break;
			case 240:
				data->keycode = KEY_HOMEPAGE;
				input_report_key(data->input_key_dev, data->keycode, 1);
				input_sync(data->input_key_dev);
				data->report_flag = 1;
				break;
			case 400:
				data->keycode = KEY_BACK;
				input_report_key(data->input_key_dev, data->keycode, 1);
				input_sync(data->input_key_dev);
				data->report_flag = 1;
				break;
			default:
				data->keycode = 0;
				break;
		}
	}else{

	switch(event->touch_point) {
		case 5:
			if(event->y5 >860){
			}else{
			//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,5);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y5);
	//		input_report_key(data->input_dev, BTN_TOUCH, 1);
			input_mt_sync(data->input_dev);
			ctp_debug_info("===x5 = %d,y5 = %d ====\n",event->x5,event->y5);
			}
		case 4:
			if(event->y4 >860){
			}else{
			//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,4);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x4);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y4);
	//		input_report_key(data->input_dev, BTN_TOUCH, 1);
			//input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			ctp_debug_info("===x4 = %d,y4 = %d ====\n",event->x4,event->y4);
			}
		case 3:
			if(event->y3 >860){
			}else{
			//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,3);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x3);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y3);
	//		input_report_key(data->input_dev, BTN_TOUCH, 1);
			//input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			ctp_debug_info("===x3 = %d,y3 = %d ====\n",event->x3,event->y3);
			}
		case 2:
			if(event->y2 >860){
			}else{
			//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,2);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x2);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y2);
	//		input_report_key(data->input_dev, BTN_TOUCH, 1);
			//input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			ctp_debug_info("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
			}
		case 1:
			if(event->y1 >860){
			input_mt_sync(data->input_dev);
			input_sync(data->input_dev);
			}else{
			//input_report_abs(data->input_dev,ABS_MT_TRACKING_ID,1);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x1);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y1);
	//		input_report_key(data->input_dev, BTN_TOUCH, 1);
			//input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			up_x1 = event->x1;
			up_y1 = event->y1;
			ctp_debug_info("===x1 = %d,y1 = %d ====\n",event->x1,event->y1);
			}
			slip_flag = 1;	
		default:
			//ctp_debug_info("==touch_point default =\n");
			break;
	}
#else	/* CONFIG_FT5X0X_MULTITOUCH*/
	if (event->touch_point == 1) {
		input_report_abs(data->input_dev, ABS_X, event->x1);
		input_report_abs(data->input_dev, ABS_Y, event->y1);
		input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
	}
//	input_report_key(data->input_dev, BTN_TOUCH, 1);
#endif	/* CONFIG_FT5X0X_MULTITOUCH*/
	input_sync(data->input_dev);
		}

		release_flag = 1;
}	/*end ft5x0x_report_value*/
#if 0
void emi_on(void)
{
	if(open_emi_on !=0)
		return;
	open_emi_on++;
	ft5x0x_write_reg(0x86 , 0x02);
}
void emi_off(void)
{
	if(open_emi_on == 0)
		return;
	open_emi_on=0;
	ft5x0x_write_reg(0x86 , 0x01);
}
#endif

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;
	//ctp_debug_info("==work 1=\n");
//	if(is_usb_pluged()!= 0)
//		emi_on();
//	else
//		emi_off();
		ret = ft5x0x_read_data();	
		if (ret == 0) {	
		ft5x0x_report_value();
		}
	//ctp_debug_info("==work 2=\n");
	//    	msleep(1);
	//    enable_irq(this_client->irq);
	//enable_irq(MSM_GPIO_TO_INT(FT5x06_IRQ));
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x0x_ts_data *ft5x0x_ts = dev_id;
	//    	disable_irq(this_client->irq);		
	//disable_irq_nosync(this_client->irq);
	//ctp_debug_info("==int=\n");

	if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}


	return IRQ_HANDLED;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *ft5x0x_ts = container_of(handler, struct ft5x0x_ts_data,
			early_suspend);
	int ret;

	ctp_debug_info("==ft5x0x_ts_suspend=\n");
	if(!start)
	{
		ft5x0x_write_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
		msleep(10);
		disable_irq(this_client->irq);
		vreg_disable(vreg_touch); 
		ret = cancel_work_sync(&ft5x0x_ts->pen_event_work);
		if(ret)
			enable_irq(this_client->irq);
//		flush_workqueue(ft5x0x_ts->ts_workqueue);
	}
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
void reset_tp(int nummy)
{
	printk("%s\n",__func__);
	gpio_set_value(FT5x06_WAKEUP, 0);
	mdelay(1);
	gpio_set_value(FT5x06_WAKEUP, 1);
}
static void ft5x0x_ts_resume(struct early_suspend *handler)
{
	ctp_debug_info("==ft5x0x_ts_resume=\n");
	if(release_flag)
	{
		ft5x0x_ts_release();
	}
			
	vreg_enable(vreg_touch); 
	gpio_set_value(26, 0);
	msleep(1);
	gpio_set_value(26, 1);
	msleep(200);
	ft5x0x_write_reg(FT5X0X_REG_PMODE, PMODE_MONITOR);
	msleep(20);
	enable_irq(this_client->irq);
}
#endif  //CONFIG_HAS_EARLYSUSPEND
#if 0
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
/*Start: WNC Edware 2010/01/26 added to show ctp information*/
static ssize_t ctp_information_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *s = buf;
	//char ic_num[2];

	FW_Ver = ft5x0x_read_fw_ver();
	W27_VENDOR_ID = read_vendor_ID();
	s += sprintf(s, "CTP Vendor id: %d\n",W27_VENDOR_ID);
	if(W27_VENDOR_ID==0x51){  /*WNC_Mike 20100319, add to read more infomation for CTP FT5201.*/
		s += sprintf(s,"CTP Vendor: %s\n", "FocalTech");
		s += sprintf(s,"IC Number: FT%.2d%.2d\n",53,06);
		//s += sprintf(s,"IC Number: FT%.2d%.2d\n",IC_NUM_1,IC_NUM_2);
	}else if(W27_VENDOR_ID==0x5C){  /*WNC_Mike 20100319, add to read more infomation for CTP FT5201.*/
		s += sprintf(s,"CTP Vendor: %s\n", "TPK");
	}else{
		s += sprintf(s,"CTP Vendor: %s\n", "Unknown");	
		s += sprintf(s,"IC Number: %s\n", "Unknown");
	}
	/*WNC_Mike 20100605, modify for Cypress FW_Ver_SUB expression.-----START*/
	if(W27_VENDOR_ID==0x5C)  
		;
	//s += sprintf(s, "FW Version: %d.%d\n", FW_Ver,(FW_Ver_SUB - (FW_Ver_SUB/16)*6));  //Show number of HEX on screen
	else
		/*END-------WNC_Mike 20100605, modify for Cypress FW_Ver_SUB expression.*/	
		s += sprintf(s, "FW Version: %d.%d\n", 02,FW_Ver);
	//s += sprintf(s, "FW Version: %d.%d\n", FW_Ver,FW_Ver_SUB);
	if(W27_CTP_TYPE == 1)
		s += sprintf(s,"CTP Type: %s\n", "Polling");
	else if(W27_CTP_TYPE == 2)
		s += sprintf(s,"CTP Type: %s\n", "Interrupt trigger");
	else
		s += sprintf(s,"CTP Type: %s\n", "Unknown");

	return (s - buf);
}

	static ssize_t
ctp_information_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	//Do nothing, because we just want to query by user
	return 0;
}

ctp_info(ctp_information);

struct kobject *ctp_kobj;
static struct attribute * g[] = {
	&ctp_information_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

static int __init ctp_init(void)
{
	ctp_kobj = kobject_create_and_add("ctp", NULL);
	if (!ctp_kobj)
		return -ENOMEM;
	return sysfs_create_group(ctp_kobj, &attr_group);
}
#endif
#if SYS_DEBUG

static ssize_t fts_information_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	char *s = buf;

	FW_Ver = ft5x0x_read_fw_ver();
	W27_VENDOR_ID = read_vendor_ID();

	s += sprintf(s, "CTP Vendor id: %d\n",W27_VENDOR_ID);
	if(W27_VENDOR_ID==0x51){  /*WNC_Mike 20100319, add to read more infomation for CTP FT5201.*/
		s += sprintf(s,"CTP Vendor: %s\n", "OFILE");
		
	}else if(W27_VENDOR_ID==0x53){  /*WNC_Mike 20100319, add to read more infomation for CTP FT5201.*/
		s += sprintf(s,"CTP Vendor: %s\n", "MUDONG");
	}else if(W27_VENDOR_ID==0xa0)
	{
		s += sprintf(s,"CTP Vendor: %s\n", "TOPTOUCH");
	}
	else{
		s += sprintf(s,"CTP Vendor: %s\n", "Unknown");	

	}

		s += sprintf(s, "IC number: %.2d%.2d\n",53,16);
		s += sprintf(s, "FW Version: %d.%d\n", 02,FW_Ver);
	if(W27_CTP_TYPE == 1)
		s += sprintf(s,"CTP Type: %s\n", "Polling");
	else if(W27_CTP_TYPE == 2)
		s += sprintf(s,"CTP Type: %s\n", "Interrupt trigger");
	else
		s += sprintf(s,"CTP Type: %s\n", "Unknown");

	return (s - buf);
}

/*upgrade from *.i*/
static ssize_t fts_information_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{

	return -EPERM;
}
static ssize_t ft6x06_tprwreg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	/*place holder for future use*/
	return -EPERM;
}

static ssize_t ft6x06_tprwreg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	ssize_t num_read_chars = 0;
	int retval;
	long unsigned int wmreg = 0;
	u8 regaddr = 0xff, regvalue = 0xff;
	u8 valbuf[5] = {0};

	memset(valbuf, 0, sizeof(valbuf));
	mutex_lock(&g_device_mutex);
	num_read_chars = count - 1;

	if (num_read_chars != 2) {
		if (num_read_chars != 4) {
			pr_info("please input 2 or 4 character\n");
			goto error_return;
		}
	}

	memcpy(valbuf, buf, num_read_chars);
	retval = strict_strtoul(valbuf, 16, &wmreg);

	if (0 != retval) {
		dev_err(&client->dev, "%s() - ERROR: Could not convert the "\
						"given input to a number." \
						"The given input was: \"%s\"\n",
						__func__, buf);
		goto error_return;
	}

	if (2 == num_read_chars) {
		/*read register*/
		regaddr = wmreg;
		if (ft5x0x_read_reg(regaddr, &regvalue) < 0)
			dev_err(&client->dev, "Could not read the register(0x%02x)\n",
						regaddr);
		else
			pr_alert("the register(0x%02x) is 0x%02x\n",
					regaddr, regvalue);
	} else {
		regaddr = wmreg >> 8;
		regvalue = wmreg;
		if (ft5x0x_write_reg(regaddr, regvalue) < 0)
			dev_err(&client->dev, "Could not write the register(0x%02x)\n",
							regaddr);
		else
			dev_err(&client->dev, "Write 0x%02x into register(0x%02x) successful\n",
							regvalue, regaddr);
	}

error_return:
	mutex_unlock(&g_device_mutex);

	return count;
}

static ssize_t ft6x06_fwupdate_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	/* place holder for future use */
	return -EPERM;
}

/*upgrade from *.i*/
static ssize_t ft6x06_fwupdate_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ft5x0x_ts_data *data = NULL;
	u8 uc_host_fm_ver;
	u8 uc_tp_vendor_id;
	int i_ret;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	data = (struct ft5x0x_ts_data *)i2c_get_clientdata(client);

	uc_tp_vendor_id = read_vendor_ID();
	mutex_lock(&g_device_mutex);
	disable_irq(client->irq);
		msleep(100);
		if(uc_tp_vendor_id == 0xa8 || uc_tp_vendor_id == 0)
			uc_tp_vendor_id = FTS_CTPM_GetVendorID_Info();
		i_ret = fts_ctpm_fw_upgrade_with_i_file(uc_tp_vendor_id);
		if (i_ret == 0)
	 	{
			msleep(300);
			uc_host_fm_ver = fts_ctpm_get_i_file_ver();
			printk("[FTS] upgrade to new version 0x%x\n", uc_host_fm_ver);
		}
		else
		{
			printk("[FTS] upgrade failed ret=%d.\n", i_ret);
			printk("%s\n",__func__);
			gpio_set_value(FT5x06_WAKEUP, 0);
			mdelay(1);
			gpio_set_value(FT5x06_WAKEUP, 1);
			msleep(500);
		}
	enable_irq(client->irq);
	mutex_unlock(&g_device_mutex);

	return count;
}
/*sysfs */
/*get the tp information
*example:cat ctp_information
*/
static DEVICE_ATTR(ftstpinfo, S_IRUGO | S_IWUSR, fts_information_show,
			fts_information_store);


/*upgrade from *.i
*example: echo 1 > ftsfwupdate
*/
static DEVICE_ATTR(ftsfwupdate, S_IRUGO | S_IWUSR, ft6x06_fwupdate_show,
			ft6x06_fwupdate_store);

/*read and write register
*read example: echo 88 > ftstprwreg ---read register 0x88
*write example:echo 8807 > ftstprwreg ---write 0x07 into register 0x88
*
*note:the number of input must be 2 or 4.if it not enough,please fill in the 0.
*/
static DEVICE_ATTR(ftstprwreg, S_IRUGO | S_IWUSR, ft6x06_tprwreg_show,
			ft6x06_tprwreg_store);
static struct attribute *ft6x06_attributes[] = {
	&dev_attr_ftsfwupdate.attr,
	&dev_attr_ftstprwreg.attr,
	&dev_attr_ftstpinfo.attr,
	NULL
};

static struct attribute_group ft6x06_attribute_group = {
	.attrs = ft6x06_attributes
};

/*create sysfs for debug*/
int ft6x06_create_sysfs(struct i2c_client *client)
{
	int err;
	err = sysfs_create_group(&client->dev.kobj, &ft6x06_attribute_group);
	if (0 != err) {
		dev_err(&client->dev,
					 "%s() - ERROR: sysfs_create_group() failed.\n",
					 __func__);
		sysfs_remove_group(&client->dev.kobj, &ft6x06_attribute_group);
		return -EIO;
	} else {
		mutex_init(&g_device_mutex);
		pr_info("ft6x06:%s() - sysfs_create_group() succeeded.\n",
				__func__);
	}
	return err;
}

void ft6x06_release_sysfs(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &ft6x06_attribute_group);
	mutex_destroy(&g_device_mutex);
}

#endif
static int ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	struct input_dev *gesture_input_dev;
	int err = 0;
	//unsigned char uc_reg_value; 

	ctp_debug_info("==ft5x0x_ts_probe=\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ctp_debug_info("==kzalloc=\n");
	ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}


	this_client = client;
	i2c_set_clientdata(client, ft5x0x_ts);


	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);

	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		ctp_debug_info("==create_singlethread_workqueue failed=\n");
		goto exit_create_singlethread;
	}

	err = request_irq(client->irq, ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", ft5x0x_ts);
	if (err < 0) {
		dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	disable_irq(client->irq);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		ctp_debug_info("==input_allocate_device failed=\n");
		goto exit_input_dev_alloc_failed;
	}

	ft5x0x_ts->input_dev = input_dev;

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
//	set_bit(BTN_TOUCH, input_dev->keybit);

#ifdef CONFIG_FT5X0X_MULTITOUCH
	//set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	//set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	//set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	//set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_set_abs_params(input_dev,
			ABS_MT_POSITION_X, 0, 480, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_POSITION_Y, 0, 854, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
	//input_set_abs_params(input_dev,ABS_MT_TRACKING_ID, 0, 5, 0, 0);
#else
	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);

	input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
			ABS_PRESSURE, 0, PRESS_MAX, 0 , 0);
#endif


	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		ctp_debug_info("==input_register_device failed=\n");
		goto exit_input_register_device_failed;
	}

	//[--WNC Ophas 2011-08-18, for gesture key of touch screen B area
	gesture_input_dev = input_allocate_device();
	if (!gesture_input_dev) {
		err = -ENOMEM;
		ctp_debug_info("==input_allocate_device for gesture key failed=\n");
		goto exit_gesture_input_dev_alloc_failed;
	}

	ft5x0x_ts->input_key_dev = gesture_input_dev;

	//set possible key
	set_bit(KEY_BACK, gesture_input_dev->keybit);
	set_bit(KEY_MENU, gesture_input_dev->keybit);
	set_bit(KEY_HOMEPAGE, gesture_input_dev->keybit);
	set_bit(KEY_LTR, gesture_input_dev->keybit);
	set_bit(KEY_UTD, gesture_input_dev->keybit);
	set_bit(KEY_SC, gesture_input_dev->keybit);

	//set possible event type
	set_bit(EV_KEY, gesture_input_dev->evbit);

	gesture_input_dev->name		= FT5X0X_GESTURE_NAME;		//dev_name(&client->dev)
	// [ -- WNC-NJ:Peitao add input register function for gesture  @ 20110822
	err = input_register_device(gesture_input_dev);
	if (err) {
		ctp_debug_info("==input_register_device gesture key failed=\n");
		goto exit_gesture_input_register_device_failed;
	}
	// -- WNC-NJ:Peitao add input register function for gesture  @ 20110822 ]
	//WNC Ophas 2011-08-18, for gesture key of touch screen B area--]

#ifdef CONFIG_HAS_EARLYSUSPEND
	ctp_debug_info("==register_early_suspend =\n");
	ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 5;
	ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
	ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
	register_early_suspend(&ft5x0x_ts->early_suspend);
#endif

	msleep(200);
	//get some debug information from sys
	#if SYS_DEBUG
	ft6x06_create_sysfs(client);
	#endif
	//ctp_init();
	// uc_reg_value = ft5x0x_read_fw_ver();
	FW_Ver = ft5x0x_read_fw_ver();
	ctp_debug_info("[FST] Firmware version = 0x%x\n", FW_Ver);
	gpio_set_value(FT5x06_WAKEUP, 0);
	mdelay(1);
	gpio_set_value(FT5x06_WAKEUP, 1);
	mdelay(300);
#if CFG_SUPPORT_AUTO_UPG
	if(FW_Ver != 0xa6)
	fts_ctpm_auto_upg();
#endif
	
	enable_irq(client->irq);

	ctp_debug_info("==probe over =\n");
	return 0;

exit_gesture_input_register_device_failed:
	input_free_device(gesture_input_dev);
exit_gesture_input_dev_alloc_failed:
exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	//	free_irq(client->irq, ft5x0x_ts);
	free_irq(MSM_GPIO_TO_INT(FT5x06_IRQ), ft5x0x_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
	ctp_debug_info("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static int __devexit ft5x0x_ts_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);

	ctp_debug_info("==ft5x0x_ts_remove=\n");
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
	//	free_irq(client->irq, ft5x0x_ts);
	#if SYS_DEBUG
	ft6x06_release_sysfs(client);
	#endif
	free_irq(MSM_GPIO_TO_INT(FT5x06_IRQ), ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{ FT5X0X_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= __devexit_p(ft5x0x_ts_remove),
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
	},
};

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
#if 0
static int active_ctp_fw_update_set(const char *val, struct kernel_param *kp)
{
	int result = 0;
	int i;
	unsigned char ver = 0;
	param_set_int(val, kp);
	if(start)
	{	
		struct file * fd = NULL;
		struct inode *inode = NULL;
		mm_segment_t orig_fs;
		//wake up CTP make it can update FW
		gpio_set_value(FT5x06_WAKEUP, 0);
		msleep(1);
		gpio_set_value(FT5x06_WAKEUP, 1);
		msleep(300);

		fd = filp_open("/sdcard/ft_app.bin", O_RDONLY, 0);
		inode = fd->f_dentry->d_inode;
		fsize = inode->i_size;
		printk("size=%d/n",(int)fsize);
		buf = (unsigned char *)kzalloc(fsize, GFP_KERNEL);
		orig_fs = get_fs();
		set_fs(KERNEL_DS);
		result = fd->f_op->read(fd, buf, fsize, &fd->f_pos);
		set_fs(orig_fs);
		printk("read size=%d/n", result);
		for(i=0; i<3; i++)	//��ͼdown3�Σ����ʧ������
		{
			printk("read i=%d/n", i);
			result = fts_ctpm_fw_upgrade_with_i_file();
			if(!result)//update fw ok
				break;
		}
		if(result)
		{
			printk("==update CTP fw error==\n");
			start = 0;
		}
		else
		{	start = 0;	//down ok disable it
			printk("==update CTP fw OK==\n");
		}
		kfree(buf);
		filp_close(fd, NULL);
		//suspend CTP
		for(i=0; i<3; i++) {		
			ft5x0x_read_reg(FT5X0X_REG_PMODE, &ver);
			printk("======FT5X0X_REG_PMODE= 0x%.2x\n",ver);
			if(ver == 0x03)
				break;
			ft5x0x_write_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
			msleep(10);
		}
		//suspend CTP		
	}	
	return result;
}
module_param_call(updates_CTP_fw, active_ctp_fw_update_set, param_get_int,
		&start, S_IWUSR | S_IRUGO);
#endif
static int CTP_DEBUG_ENABLE_SET(const char *val, struct kernel_param *kp)
{
	param_set_int(val, kp);
	ctp_debug_info("==ctp_debug_info_enable is %d==\n",ctp_debug_info_enable);
	return 0;
}
module_param_call(CTP_DEBUG_ENABLE, CTP_DEBUG_ENABLE_SET, param_get_int,
		&ctp_debug_info_enable, S_IWUSR | S_IRUGO);


static int __init ft5x0x_ts_init(void)
{
	int ret;
	int irq_pin = FT5x06_IRQ, wakeup_pin = FT5x06_WAKEUP;

	unsigned irq_cfg = GPIO_CFG(irq_pin, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA);	
	unsigned wakeup_cfg = GPIO_CFG(wakeup_pin, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA);   

	ctp_debug_info("==ft5x0x_ts_init==\n");

	vreg_touch= vreg_get(NULL, "gp2");
	ret = vreg_set_level(vreg_touch, 2850);
	if (ret) {
		ctp_debug_info("==ft5x0x_ts_init==verg fail\n");
		return ret;
	}

	if (IS_ERR(vreg_touch)) {
		ctp_debug_info(KERN_ERR "%s: vreg get failed (%ld)\n",
				__func__, PTR_ERR(vreg_touch));
		return -EINVAL;
	}	
	ret = gpio_request(irq_pin, "FT5x06_irq");
	if (ret) {
		ctp_debug_info("%s: gpio_request failed on pin %d (rc=%d)\n", __func__, irq_pin, ret);
	}

	ret= gpio_request(wakeup_pin, "FT5x06_wakeup");
	if (ret) {
		ctp_debug_info("%s: gpio_request failed on pin %d (rc=%d)\n", __func__, wakeup_pin, ret);
	}

	ret = gpio_tlmm_config(irq_cfg, GPIO_CFG_ENABLE);
	if (ret) {
		ctp_debug_info("%s:gpio_tlmm_config failed on pin %d (rc=%d)\n", __func__, irq_pin, ret);
	}

	ret = gpio_tlmm_config(wakeup_cfg, GPIO_CFG_ENABLE);
	if (ret) {
		ctp_debug_info("%s:gpio_tlmm_config failed on pin %d (rc=%d)\n", __func__, wakeup_pin, ret);
	}

	ret = vreg_enable(vreg_touch); 
	gpio_set_value(wakeup_pin, 0);
	msleep(20);
	gpio_set_value(wakeup_pin, 1);

	if(ret)
		ctp_debug_info("%s:vreg_enable failed  (rc=%d)\n", __func__,  ret);

	ret = i2c_add_driver(&ft5x0x_ts_driver);
	ctp_debug_info("ret=%d\n",ret);
	return ret;
	//	return i2c_add_driver(&ft5x0x_ts_driver);
}

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void __exit ft5x0x_ts_exit(void)
{
	ctp_debug_info("==ft5x0x_ts_exit==\n");
	i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
