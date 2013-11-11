#ifndef _SPI_DEV_H_
#define _SPI_DEV_H_

#include "spi_main.h"

#ifdef SPI_FEATURE_SC8800G
#include "os_api.h"
#include "gpio_drvapi.h"		/* To use GPIO Callback */
#include "gpio_prod_cfg.h"		/* To use GPIO interface */
#include "spi_slave_drvapi.h"		/* To use SPI Slave API */
#include "pinmap_drvapi.h"		/* To use GPIO value */
#endif

#ifdef SPI_FEATURE_OMAP4430
#include <linux/interrupt.h>
#endif

#define SPI_DEV_MAX_PACKET_SIZE 2048 * 6

typedef enum
{
	SPI_DEV_SYNC_OFF = 0,
	SPI_DEV_SYNC_ON 
} SPI_DEV_SYNC_STATE_T;

typedef enum
{
	SPI_DEV_GPIOLEVEL_LOW	= 0,
	SPI_DEV_GPIOLEVEL_HIGH
}SPI_DEV_GPIOLEVEL_T;


typedef enum
{
	SPI_DEV_IRQ_TRIGGER_RISING	= 1,
	SPI_DEV_IRQ_TRIGGER_FALLING
}SPI_DEV_IRQ_TRIGGER_T;


#ifdef SPI_FEATURE_SC8800G
#define	SPI_DEV_IRQ_HANDLER_T	GPIO_CALLBACK
#define SPI_DEV_IRQ_HANDLER(X) int X (uint32 tmp)
#elif defined SPI_FEATURE_OMAP4430
typedef irqreturn_t (*SPI_DEV_IRQ_HANDLER_T)(int, void *);
#define SPI_DEV_IRQ_HANDLER(X) irqreturn_t X ( int irq, void *data )
#endif


extern int spi_dev_gpio_mrdy;		
extern int spi_dev_gpio_srdy;		
extern int spi_dev_gpio_submrdy;	
extern int spi_dev_gpio_subsrdy;	

#ifdef SPI_FEATURE_MASTER
extern int spi_is_restart;
#endif

extern void spi_dev_init (void * data);
extern void spi_dev_destroy( void );
extern int spi_dev_send ( void * buf, unsigned int length );
extern int spi_dev_receive ( void * buf, unsigned int length );
extern void spi_dev_set_gpio ( int gpio_id, SPI_DEV_GPIOLEVEL_T value );
extern SPI_DEV_GPIOLEVEL_T spi_dev_get_gpio (int gpio_id);
extern int spi_dev_reigster_irq_handler (int gpio_id, SPI_DEV_IRQ_TRIGGER_T trigger, SPI_DEV_IRQ_HANDLER_T handler, char * name, void * data);
extern void spi_dev_unreigster_irq_handler (int gpio_id, void * data);

#endif
