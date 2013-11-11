#include "spi_main.h"
#include "spi_dev.h"
#include "spi_os.h"

#ifdef SPI_FEATURE_SC8800G

#elif defined SPI_FEATURE_OMAP4430
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/phone_svn/ipc_spi.h>
#endif

int spi_dev_gpio_mrdy 		= -1;		
int spi_dev_gpio_srdy 		= -1;		
int spi_dev_gpio_submrdy 	= -1;	
int spi_dev_gpio_subsrdy 	= -1;	

#ifdef SPI_FEATURE_MASTER
int spi_is_restart				= 0;
#endif

#ifdef SPI_FEATURE_MASTER
static SPI_DEV_GPIOLEVEL_T _gpio_mrdy_state		= SPI_DEV_GPIOLEVEL_LOW;
static SPI_DEV_GPIOLEVEL_T _gpio_submrdy_state	= SPI_DEV_GPIOLEVEL_LOW;
#else
static SPI_DEV_GPIOLEVEL_T _gpio_srdy_state		= SPI_DEV_GPIOLEVEL_LOW;
static SPI_DEV_GPIOLEVEL_T _gpio_subsrdy_state	= SPI_DEV_GPIOLEVEL_LOW;
#endif

/*
//init variables and status
*/
#ifdef SPI_FEATURE_SC8800G
extern void _start_packet_tx_callback( void );
extern void _start_packet_rx_callback( void );
#endif


void spi_dev_init ( void * data)
{
#ifdef SPI_FEATURE_SC8800G
{
	SPI_SLAVE_DEV dev;
	SPI_OS_TRACE(("spi_dev_init \n"));

	spi_dev_gpio_mrdy 		= GPIO_PROD_APCP_MRDY;
	spi_dev_gpio_srdy	 	= GPIO_PROD_APCP_SRDY;
	spi_dev_gpio_submrdy 	= GPIO_PROD_APCP_SubMRDY;
	spi_dev_gpio_subsrdy	= GPIO_PROD_APCP_SubSRDY;

	// Open SPI interface
	dev.mode = 1;		/* CPOL=0, CPHA=1 */
	dev.tx_bit_length = 32;	/* Transmit data Length */
	dev.tx_callback = _start_packet_tx_callback;
	dev.rx_callback = _start_packet_rx_callback;
	SPI_SLAVE_HAL_Open(&dev); 

	spi_dev_set_gpio ( spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW );
	spi_dev_set_gpio ( spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW );
}	
#elif defined SPI_FEATURE_OMAP4430
{
	struct ipc_spi_platform_data * pdata;

	SPI_OS_TRACE(("spi_dev_init \n"));

	pdata = (struct ipc_spi_platform_data *) data;

	spi_dev_gpio_mrdy 		= (int) pdata->gpio_ipc_mrdy;
	spi_dev_gpio_srdy 		= (int) pdata->gpio_ipc_srdy;
	spi_dev_gpio_submrdy 	= (int) pdata->gpio_ipc_sub_mrdy;
	spi_dev_gpio_subsrdy	= (int) pdata->gpio_ipc_sub_srdy;

	spi_dev_set_gpio ( spi_dev_gpio_mrdy, SPI_DEV_GPIOLEVEL_LOW );
	if ( spi_is_restart == 0 )
	{
		spi_dev_set_gpio ( spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_LOW );
	}
}
#endif  
}


void spi_dev_destroy( void )
{
#if defined SPI_FEATURE_OMAP4430	
	spi_dev_unreigster_irq_handler(spi_dev_gpio_srdy, 0);
	spi_dev_unreigster_irq_handler(spi_dev_gpio_subsrdy, 0);
#endif
}

int spi_dev_send ( void * buf, unsigned int length )
{
#ifdef SPI_FEATURE_SC8800G
	uint32 spi_write_result = 0;

	spi_write_result = SPI_SLAVE_HAL_Write(buf, length);

	if ( spi_write_result == length ) 
		return 0; 				// success
	else 
	{
		SPI_OS_ERROR (("spi_dev_send fail length %d/%d\n", spi_write_result, length));
		return spi_write_result;	// fail
	}
#elif defined SPI_FEATURE_OMAP4430
	extern int ipc_spi_tx_rx_sync( u8 *tx_d, u8 *rx_d, unsigned len );
	int result;

	result = ipc_spi_tx_rx_sync (buf, 0, length);
	return result;
#endif
}


int spi_dev_receive ( void * buf, unsigned int length)
{
#ifdef SPI_FEATURE_SC8800G
	uint32 spi_read_result = 0;

	spi_read_result = SPI_SLAVE_HAL_Read(buf, length);

	if ( spi_read_result == length ) 
		return 0; 				// success
	else 
	{
		SPI_OS_ERROR (("spi_dev_send faile length %d/%d\n", spi_read_result, length));
		return spi_read_result;		// fail
	}
		
#elif defined SPI_FEATURE_OMAP4430
	extern int ipc_spi_tx_rx_sync( u8 *tx_d, u8 *rx_d, unsigned len );
	int value;

	value =  ipc_spi_tx_rx_sync (0, buf, length);

	return value;
#endif
}


/*
//Param input	gpio_id	: number of gpio id
//Value	: SPI_DEV_GPIOLEVEL_HIGH for raising pin state up
//		: SPI_DEV_GPIOLEVEL_LOW for getting pin state down
*/
void spi_dev_set_gpio ( int gpio_id, SPI_DEV_GPIOLEVEL_T value )
{
	SPI_OS_TRACE_MID(("spi_dev_set_gpio: gpio_id =[%d], value =[%d]\n",gpio_id, (int) value));

#ifdef SPI_FEATURE_SC8800G
{
	unsigned char level = 0;
	GPIO_CFG_INFO_T_PTR cfg_ptr = GPIO_PROD_GetCfgInfo ((GPIO_PROD_ID_E)gpio_id);

	switch (value)
	{
		case SPI_DEV_GPIOLEVEL_LOW : level = 0; break;
		case SPI_DEV_GPIOLEVEL_HIGH : level = 1; break;
	}

	GPIO_SetValue(cfg_ptr->gpio_num, level);
}	
#elif defined SPI_FEATURE_OMAP4430
{
	int level = 0;

	switch (value)
	{
		case SPI_DEV_GPIOLEVEL_LOW : level = 0; break;
		case SPI_DEV_GPIOLEVEL_HIGH : level = 1; break;
	}

	gpio_set_value ((unsigned int) gpio_id, level);
}	
#endif
}


/*
// Log should be removed for loop
//Param input	gpio_id	: gpio pin id
//Return value	SPI_DEV_GPIOLEVEL
*/
SPI_DEV_GPIOLEVEL_T spi_dev_get_gpio(int gpio_id)
{
	SPI_DEV_GPIOLEVEL_T value = SPI_DEV_GPIOLEVEL_LOW;
	int level;
	
#ifdef SPI_FEATURE_SC8800G
{
	GPIO_CFG_INFO_T_PTR cfg_ptr = GPIO_PROD_GetCfgInfo ((GPIO_PROD_ID_E)gpio_id);

	level =  GPIO_GetValue (cfg_ptr->gpio_num);

	switch (level)
	{
		case 0 : value = SPI_DEV_GPIOLEVEL_LOW; break;
		case 1 : value = SPI_DEV_GPIOLEVEL_HIGH; break;
	}
}
#elif defined SPI_FEATURE_OMAP4430
	level =  gpio_get_value ((unsigned int) gpio_id);

	switch (level)
	{
		case 0 : value = SPI_DEV_GPIOLEVEL_LOW; break;
		case 1 : value = SPI_DEV_GPIOLEVEL_HIGH; break;
	}
#endif

	return value;
}


/*
//Param input	gpio_id	: gpio pin id
		tigger	: interrupt detection mode
		handler	: interrupt handler function
//Return value	0	: fail
				1	: success
*/
int spi_dev_reigster_irq_handler (int gpio_id, SPI_DEV_IRQ_TRIGGER_T trigger, SPI_DEV_IRQ_HANDLER_T handler, char * name, void * data)
{
	int value;

#ifdef SPI_FEATURE_SC8800G
	value = GPIO_PROD_RegGpio(
			(GPIO_PROD_ID_E)gpio_id,	/* ID */
			SCI_FALSE,		/* Direction, SCI_FALSE: Input dir; SCI_TRUE: output dir */
			SCI_FALSE,		/* Default_value */
			SCI_FALSE,		/* Shaking Enable */
			NULL,			/* Shaking interval */
			(SPI_DEV_IRQ_HANDLER_T)handler	/* the type of handler should be compatable with SPRD */
		);
	if ( value == SCI_FALSE )
	{
		SPI_OS_ERROR(("spi_dev_reigster_irq_handler: registration fail\n"));
		return 0;
	}

#elif defined SPI_FEATURE_OMAP4430
{
	int _trigger = IRQF_TRIGGER_NONE;

	switch (trigger)
	{
		case SPI_DEV_IRQ_TRIGGER_RISING : _trigger = IRQF_TRIGGER_RISING; break;
		case SPI_DEV_IRQ_TRIGGER_FALLING : _trigger = IRQF_TRIGGER_FALLING; break;
		default : _trigger = IRQF_TRIGGER_NONE; break;
	}

	value = request_irq (OMAP_GPIO_IRQ( gpio_id ), handler, _trigger, name, data);
	if (value != 0)
	{
		SPI_OS_ERROR(("spi_dev_reigster_irq_handler: registration fail (err %d)\n", value));
		return 0;
	}
}	
#endif
	return 1;
}


void spi_dev_unreigster_irq_handler (int gpio_id, void * data)
{

#ifdef SPI_FEATURE_SC8800G
#elif defined SPI_FEATURE_OMAP4430
	free_irq (OMAP_GPIO_IRQ( gpio_id ), data);
#endif
}

