#include <asm/io.h>
#include <linux/clocksource.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include <plat/mux.h>

#define REG_VCELL	0x2
#define REG_SOC     0x4
#define REG_MODE	0x6
#define REG_VERSION 0x8
#define REG_RCOMP	0xC
#define REG_COMMAND	0xFE

/*GPADC*/
#define TWL6030_GPADC_CTRL_P2		0x06
#define TWL6030_GPADC_CTRL_P2_SP2	(1 << 2)
#define TWL6030_GPADC_EOC_SW		(1 << 1)
#define TWL6030_GPADC_BUSY		(1 << 0)
#define TWL6030_GPADC_MAX_CHANNELS  17
#define TWL6030_GPADC_GPCH0_LSB		(0x29)



#define REG_TOGGLE1	0x90
#define GPADCS		(1 << 1)
#define GPADCR		(1 << 0)



struct madc_chrequest {
	int channel;
	unsigned int data;
	int is_average;
};


/*
 * actual scaler gain is multiplied by 8 for fixed point operation
 * 1.875 * 8 = 15
 */
static const u16 twl6030_gain[17] = {
	1142,	/* CHANNEL 0 */
	8,	/* CHANNEL 1 */

	/* 1.875 */
	15,	/* CHANNEL 2 */

	8,	/* CHANNEL 3 */
	8,	/* CHANNEL 4 */
	8,	/* CHANNEL 5 */
	8,	/* CHANNEL 6 */

	/* 5 */
	40,	/* CHANNEL 7 */

	/* 6.25 */
	50,	/* CHANNEL 8 */

	/* 11.25 */
	90,	/* CHANNEL 9 */

	/* 6.875 */
	55,	/* CHANNEL 10 */

	/* 1.875 */
	15,	/* CHANNEL 11 */
	8,	/* CHANNEL 12 */
	8,	/* CHANNEL 13 */

	/* 6.875 */
	55,	/* CHANNEL 14 */

	8,	/* CHANNEL 15 */
	8,	/* CHANNEL 16 */
};

struct madc_chrequest adc_request[6]={
	{
	.channel=0,
	.is_average=0,
	},
	{
	.channel=1,
	.is_average=0,
	},
	{
	.channel=2,
	.is_average=0,
	},
	{
	.channel=3,
	.is_average=0,
	},
	{
	.channel=4,
	.is_average=0,
	},
	{
	.channel=5,
	.is_average=0,
	},
};

s32 t2_write(u8 devaddr, u8 value, u8 regoffset);
s32 t2_read(u8 devaddr, u8 *value, u8 regoffset);
s32 normal_i2c_read_word(u8 devaddr, u8 regoffset, u8 *value);


s32 clear_set(u8 mod_no, u8 clear, u8 set, u8 reg);
s32 t2_madc_convert(struct madc_chrequest *ch_request, u32 count);
s32 t2_adc_data( u8 channel);
int sleep_get_max17040_battery_adc(void);
int sleep_get_max17040_battery_soc(void);
int sleep_max17040_read(u8 reg);
int get_average_adc_value(unsigned int * data, int count);


static const u32 calibration_bit_map = 0x47FF;


s32 clear_set(u8 mod_no, u8 clear, u8 set, u8 reg)
{
	s32 ret;
	u8 data = 0;

	/* Gets the initial register value */
	ret = t2_read(mod_no, &data, reg);
	if (ret)
		return ret;
	/* Clearing all those bits to clear */
	data &= ~(clear);
	/* Setting all those bits to set */
	data |= set;
	ret = t2_write(mod_no, data, reg);
	/* Update the register */
	if (ret)
		return ret;
	return 0;
}

#define NO_MADC_POWER_CTRL
s32 t2_madc_convert(struct madc_chrequest *ch_request, u32 count)
{

	u32 i=0;


	u8 val;
	u32 msb,lsb;
	s32 raw_code;
	s32 ret = 0;

	s32 gain_error;
	s32 offset_error;
	s32 corrected_code;
	s32 channel_value;

	s32 raw_channel_value;



	/* Start the conversion process */
	t2_write(0x4B, GPADCS, REG_TOGGLE1);

	ret = clear_set(0x4A, 0, TWL6030_GPADC_CTRL_P2_SP2, TWL6030_GPADC_CTRL_P2);
	if(ret)
		goto out;

	do {
		t2_read(0x4A, &val, TWL6030_GPADC_CTRL_P2);

		if (!(val & TWL6030_GPADC_BUSY) && (val & TWL6030_GPADC_EOC_SW))
			return 0;
	} while (1);


	for (i = 0; i < TWL6030_GPADC_MAX_CHANNELS; i++) {
		if (ch_request->channel & (1<<i)) {

		/* For each ADC channel, we have MSB and LSB register pair. MSB address
		 * is always LSB address+1. reg parameter is the addr of LSB register */
		 t2_read(0x4A, &msb, TWL6030_GPADC_GPCH0_LSB + 2 * i + 1);
		 t2_read(0x4A, &lsb, TWL6030_GPADC_GPCH0_LSB + 2 * i);

		 raw_code =  (int)((msb << 8) | lsb);

		 count++;

		/*
		 * multiply			by 1000 to convert the unit to milli
		 * division by 1024 (>> 10) for 10 bit ADC
		 * division by 8 (>> 3) for actual scaler gain
		 */
		 raw_channel_value = (raw_code * twl6030_gain[i]
								* 1000) >> 13;


		ch_request[i].data = raw_channel_value;

		/*
			if (~calibration_bit_map & (1 << i)) {
				req->buf[i].code = raw_code;
				req->rbuf[i] = raw_channel_value;
				continue;
			}


			gain_error = twl6030_calib_tbl[i].gain_error;
			offset_error = twl6030_calib_tbl[i].offset_error;

			corrected_code = (raw_code * SCALE - offset_error)
								/ gain_error;
			channel_value = (corrected_code * twl6030_gain[i]
								* 1000) >> 13;
			req->buf[i].code = corrected_code;
			req->rbuf[i] = channel_value;
		*/


		}
	}


out:
	return ret;


}

s32 t2_adc_data( u8 channel)
{
	s32 ret=0;

	int i;
	unsigned int data[5];

	for(i=0 ; i<5 ; i++)
	{
		ret = t2_madc_convert(&adc_request[channel], 1);
		if(ret)
			printk("[TA] Fail to get ADC data\n");

		data[i]=adc_request[channel].data;

		//printk("ADC : %d\n", data[i]);
	}

	ret = get_average_adc_value(data, 5);



	return ret;
}

int get_average_adc_value(unsigned int * data, int count)
{
	int i=0, average, min=0xFFFFFFFF, max=0, total=0;
	for(i=0 ; i<count ; i++)
	{
		if(data[i] < min)
			min=data[i];
		if(data[i] > max)
			max=data[i];

		total+=data[i];
	}

	average = (total - min -max)/(count -2);
	return average;
}
