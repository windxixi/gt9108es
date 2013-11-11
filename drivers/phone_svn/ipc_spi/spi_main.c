#include "spi_main.h"
#include "spi_os.h"
#include "spi_dev.h"
#include "spi_app.h"
#include "spi_data.h"
#include "spi_test.h"

#ifdef SPI_FEATURE_SC8800G
#include "sci_types.h"
#include "os_api.h"
#include "isr_drvapi.h"
#include "ipctask.h"
#include "gpio_prod_api.h"
#include "pwm_drvapi.h"
#include "deepsleep_drvapi.h"
#elif defined SPI_FEATURE_OMAP4430
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/wakelock.h>
#include <linux/semaphore.h>
static struct wake_lock spi_wakelock;
extern struct workqueue_struct* suspend_work_queue;
#endif

#ifdef SPI_FEATURE_CONT_SEND
#define SPI_MAIN_CONT_SEND_AP 2
#define SPI_MAIN_CONT_SEND_CP 10
#endif

#ifdef SPI_FEATURE_SC8800G
SPI_OS_THREAD_ID_T spi_main_thread_id = SCI_INVALID_BLOCK_ID;
#define MASTER_CONFIRM_WAIT_TIME 20  //20*2 = 40 msec
#define MASTER_CONFIRM_WAIT_LOOP_TIME 2 //
#ifdef SPI_FEATURE_DMA
#pragma arm section zidata = "SPI_BUFFER"
__align(4) char spi_rx_buf[SPI_DEV_MAX_PACKET_SIZE];
__align(4) char spi_tx_buf[SPI_DEV_MAX_PACKET_SIZE];
#pragma arm section zidata
#endif
#elif defined SPI_FEATURE_OMAP4430
static void spi_main_process(struct work_struct *pspi_wq);
static DEFINE_SPINLOCK(spi_lock);
#ifdef SPI_GUARANTEE_MRDY_GAP
#define MRDY_GUARANTEE_MRDY_TIME_GAP    0 // under 1ms
#define MRDY_GUARANTEE_MRDY_TIME_SLEEP  2
#define MRDY_GUARANTEE_MRDY_LOOP_COUNT	10000 // 120us

unsigned long mrdy_low_time_save, mrdy_high_time_save;

int check_mrdy_time_gap(unsigned long x, unsigned long y)
{
    if( x == y)
    {
        return 1;
    }    
    else if( (x < y) && ((y-x)<= MRDY_GUARANTEE_MRDY_TIME_GAP) )
    {
        return 1;
    }
    else if( (x > y))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}
#endif
#endif

SPI_MAIN_STATE_T spi_main_state = SPI_MAIN_STATE_END;

static int _start_data_send(void);
static void _start_packet_tx( void );
static void _start_packet_tx_send( void );
static void _start_packet_receive(void );

#ifdef SPI_FEATURE_DMA
void _start_packet_tx_callback( void );
void _start_packet_rx_callback( void );
static void _start_packet_receive_process( void );
#endif


#ifdef SPI_FEATURE_CONT_SEND	
static int spi_main_continuous_send = 0;
#endif



void spi_main_set_state (SPI_MAIN_STATE_T state)
{
	spi_main_state = state;
	SPI_OS_TRACE_MID(("spi_main_set_state %d\n", (int)state));
}


/*=====================================
//Description 	an interrupt handler for mrdy rising
=====================================*/
SPI_DEV_IRQ_HANDLER(spi_main_mrdy_rising_handler)
{
#ifdef SPI_FEATURE_SC8800G	
	int result = ISR_DONE;
#elif defined SPI_FEATURE_OMAP4430
	irqreturn_t result =  IRQ_HANDLED;
#endif

//	SPI_OS_TRACE_MID(("spi_main_mrdy_rising_handler :spi_main_state=[%d] \n",(int)spi_main_state));

#ifdef SPI_FEATURE_SLAVE
	// MRDY interrupt work on SPI_MAIN_STATE_IDLE state for receive data
	if ( spi_main_state == SPI_MAIN_STATE_IDLE || spi_main_state == SPI_MAIN_STATE_RX_TERMINATE || spi_main_state == SPI_MAIN_STATE_TX_TERMINATE
		|| spi_main_state == SPI_MAIN_STATE_TX_START )
	{
		spi_main_send_signalfront( SPI_MAIN_MSG_RECEIVE );

		return result;
	}
	else
	{
		SPI_OS_TRACE_MID(("spi_main_mrdy_rising_handler :spi_main_state=[%d] \n",(int)spi_main_state));

	}
#endif

	return result;
}


/*=====================================
//Description 	an interrupt handler for srdy rising
=====================================*/
SPI_DEV_IRQ_HANDLER(spi_main_srdy_rising_handler)
{
#ifdef SPI_FEATURE_SC8800G	
	int result = ISR_DONE;

	SPI_OS_TRACE_MID(("spi_main_srdy_rising_handler :spi_main_state=[%d]\n",(int)spi_main_state));
#elif defined SPI_FEATURE_OMAP4430
	irqreturn_t result =  IRQ_HANDLED;
	extern int send_modem_spi;
	extern struct semaphore srdy_sem;
	
	if( !wake_lock_active( &spi_wakelock ) ) 
	{
		wake_lock(&spi_wakelock);
		SPI_OS_TRACE_MID(( "[SPI] [%s](%d) spi_wakelock locked .\n",__func__, __LINE__ ));
	}

	if (send_modem_spi == 1)
	{
		up( &srdy_sem ); // signal srdy event
	}
	else
	{
		//SPI_OS_TRACE_MID(("spi_main_srdy_rising_handler :spi_main_state=[%d]\n",(int)spi_main_state)); */
	}
#endif

#ifdef SPI_FEATURE_MASTER
#if 0
	// SRDY interrupt work on SPI_MAIN_STATE_TX_WAIT state for send data
	if ( spi_main_state == SPI_MAIN_STATE_TX_WAIT || spi_main_state == SPI_MAIN_STATE_TX_START )
	{
		spi_main_send_signal( SPI_MAIN_MSG_PACKET_SEND );	
		return result;
	}
#endif
	// SRDY interrupt work on SPI_MAIN_STATE_IDLE state for receive data
	if ( spi_main_state == SPI_MAIN_STATE_IDLE 
		|| spi_main_state == SPI_MAIN_STATE_RX_TERMINATE 
		|| spi_main_state == SPI_MAIN_STATE_TX_TERMINATE
#ifdef SPI_FEATURE_CONT_SEND		
		|| spi_main_state == SPI_MAIN_STATE_RX_MORE
#endif
		)
	{
		spi_main_send_signalfront( SPI_MAIN_MSG_RECEIVE );	
		return result;
	}
#endif

	return result;
}


/*=====================================
//Description 	an interrupt handler for submrdy rising
=====================================*/
SPI_DEV_IRQ_HANDLER(spi_main_submrdy_rising_handler)
{
#ifdef SPI_FEATURE_SC8800G	
	int result = ISR_DONE;
#elif defined SPI_FEATURE_OMAP4430
	irqreturn_t result =  IRQ_HANDLED;
#endif

#ifdef SPI_FEATURE_SLAVE
	if ( spi_main_state == SPI_MAIN_STATE_TX_START )
	{
//		spi_main_send_signal(SPI_MAIN_MSG_SEND);
		spi_main_set_state ( SPI_MAIN_STATE_TX_CONFIRM );
	}
	else
	{
		SPI_OS_TRACE(("spi_main_submrdy_rising_handler :spi_main_state=[%d] \n",(int)spi_main_state));
	}
#endif
	return result;	
}


/*=====================================
//Description 	an interrupt handler for subsrdy rising
=====================================*/
SPI_DEV_IRQ_HANDLER(spi_main_subsrdy_rising_handler)
{
#ifdef SPI_FEATURE_SC8800G	
	int result = ISR_DONE;
#elif defined SPI_FEATURE_OMAP4430
	irqreturn_t result =  IRQ_HANDLED;
#endif

#ifdef SPI_FEATURE_MASTER
	// SRDY interrupt work on SPI_MAIN_STATE_TX_WAIT state for send data
	if ( spi_main_state == SPI_MAIN_STATE_TX_WAIT)
	{
		//spi_main_send_signalfront( SPI_MAIN_MSG_PACKET_SEND );	
		return result;
	}


	SPI_OS_TRACE_MID(("spi_main_subsrdy_rising_handler :spi_main_state=[%d] \n",(int)spi_main_state));
#endif

	return result;
}


/*=====================================
//Description	Send the signal to SPI Task
=====================================*/
void spi_main_send_signal (SPI_MAIN_MSG_T  spi_sigs)  
{
#ifdef SPI_FEATURE_SC8800G
	SPI_OS_MSG_T *msg;

	msg = (SPI_OS_MSG_T *)spi_os_malloc(sizeof(SPI_OS_MSG_T));
	msg->SignalCode = spi_sigs;
	msg->spi_pkt = NULL;
	
	if (SCI_SUCCESS != SCI_SendSignal(msg, spi_main_thread_id))
	{
		spi_os_free(msg);
		SPI_OS_ERROR(("Fail spi_send_signal:0x%x\n",spi_sigs));
	}
#elif defined SPI_FEATURE_OMAP4430
	struct spi_work * spi_wq = NULL;
	spi_wq = spi_os_malloc(sizeof(struct spi_work));
	spi_wq->signal_code = spi_sigs;
	INIT_WORK(&spi_wq->work, spi_main_process );
	queue_work( suspend_work_queue, spi_wq) ;
#endif	
}

/*=====================================
//Description	Send the signal to SPI Task
=====================================*/
void spi_main_send_signalfront (SPI_MAIN_MSG_T  spi_sigs)  
{
#ifdef SPI_FEATURE_SC8800G
	SPI_OS_MSG_T *msg;

	msg = (SPI_OS_MSG_T *)spi_os_malloc(sizeof(SPI_OS_MSG_T));
	msg->SignalCode = spi_sigs;
	msg->spi_pkt = NULL;
	
	if (SCI_SUCCESS != SCI_SendSignalFront(msg, spi_main_thread_id))
	{
		spi_os_free(msg);
		SPI_OS_ERROR(("Fail spi_send_signal:0x%x\n",spi_sigs));
	}
#elif defined SPI_FEATURE_OMAP4430
	struct spi_work * spi_wq = NULL;
	spi_wq = spi_os_malloc( sizeof(struct spi_work) );
	spi_wq->signal_code = spi_sigs;
	INIT_WORK(&spi_wq->work, spi_main_process );
	queue_work_front(suspend_work_queue, spi_wq);
#endif	
}


/*=====================================
//Description	check each queue data and start send routine
=====================================*/
static int _start_data_send ( void ) 
{
	if ( spi_data_check_tx_queue() == 1 )
	{
		spi_main_send_signal(SPI_MAIN_MSG_SEND);
		return 1;
	}
	
	return 0;
}

static void _start_packet_tx(void)
{
#ifdef SPI_FEATURE_SLAVE
	int i;

	if ( spi_data_check_tx_queue() == 0 )
	{
		return;
	}

#ifdef SPI_FEATURE_CONT_SEND
	if ( spi_main_state != SPI_MAIN_STATE_TX_MORE )
#endif
	{
		// check MRDY state
		if ( spi_dev_get_gpio( spi_dev_gpio_mrdy ) == SPI_DEV_GPIOLEVEL_HIGH 	
		||  spi_dev_get_gpio( spi_dev_gpio_submrdy ) == SPI_DEV_GPIOLEVEL_HIGH 	)
		{
			spi_os_sleep(MASTER_CONFIRM_WAIT_LOOP_TIME);
			_start_data_send ();
			return;
		}
	}

	// change state SPI_MAIN_STATE_IDLE to SPI_MAIN_STATE_TX_START
	spi_main_state =  SPI_MAIN_STATE_TX_START;
	

	//SPI_OS_TRACE(("==== spi Fail to receive conformation before SRDY set high ========\n"));
	
	// SRDY set high 
	spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_HIGH);

	i=0;
	while( spi_dev_get_gpio( spi_dev_gpio_submrdy ) == SPI_DEV_GPIOLEVEL_LOW )
	{
		if (i == 0)
		{
			spi_os_sleep(1);				
			i++;
			continue;
		}
		else
		 spi_os_sleep(MASTER_CONFIRM_WAIT_LOOP_TIME);

		 i++;

		if(( i > MASTER_CONFIRM_WAIT_TIME)
		||( i >= 3 && (spi_dev_get_gpio(spi_dev_gpio_mrdy) == SPI_DEV_GPIOLEVEL_HIGH)))
	 	{
			SPI_OS_ERROR(("[SPI] ERROR (Failed SLAVE TX : %d/%d)\n", i, MASTER_CONFIRM_WAIT_TIME));
			// SRDY set high 
			spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);
			spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);
			spi_main_set_state ( SPI_MAIN_STATE_IDLE );	
			_start_data_send ();
			return;
	 	}
	}


	// SUBSRDY set high 
#ifdef SPI_FEATURE_CONT_SEND
	if ( spi_main_continuous_send > 0 )
#endif
	
	spi_os_memset( (void *)spi_tx_buf, SPI_SLAVE_TX, SPI_DEV_MAX_PACKET_SIZE );
	if (spi_data_prepare_tx_packet( spi_tx_buf ) > 0)
	{
		// change state SPI_MAIN_STATE_TX_START to SPI_MAIN_STATE_TX_WAIT
		spi_main_set_state ( SPI_MAIN_STATE_TX_WAIT);

#ifdef SPI_FEATURE_CONT_SEND	
		if ( ((SPI_DATA_PACKET_HEADER_T *)spi_tx_buf)->more == 1 )
		{
			spi_main_continuous_send++;
			if ( spi_main_continuous_send >= SPI_MAIN_CONT_SEND_CP )
			{
				spi_main_continuous_send = 0;
				((SPI_DATA_PACKET_HEADER_T *)spi_tx_buf)->more = 0;
			}
		}
		else 
			spi_main_continuous_send = 0;
#endif

#ifdef SPI_FEATURE_DMA

#ifdef SPI_FEATURE_SC8800G
		SCI_SPI_EnableDeepSleep (DISABLE_DEEP_SLEEP);
#endif

		spi_dev_send( (void *)spi_tx_buf, SPI_DEV_MAX_PACKET_SIZE );

		// SUBSRDY set high 
		spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_HIGH);
		// SRDY set high 
		// spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_HIGH);
		return;
#else
		// SRDY set high 
		spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_HIGH);

		if ( spi_dev_send( (void *)spi_tx_buf, SPI_DEV_MAX_PACKET_SIZE ) != 0 )
		{	// sending fail
			// TODO: sending fail : back data to each queue
		}

		// SRDY set low
		spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);
#endif
	}
	else 
	{
		SPI_OS_ASSERT(("spi_data_prepare_tx_packet fail"));
	}

	spi_main_state =  SPI_MAIN_STATE_TX_TERMINATE;

	// SUBSRDY set low
	spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);

	// change state SPI_MAIN_STATE_TX_WAIT to SPI_MAIN_STATE_IDLE
	spi_main_state =  SPI_MAIN_STATE_IDLE;

	// Finish sending routine. check tx queue and send data
	_start_data_send();
	
#elif defined SPI_FEATURE_MASTER
	unsigned long 	flags;
	int i;
	
	if( spi_data_check_tx_queue() == 0 )
	{
		return;
	}


// check SUB SRDY state
	if ( spi_dev_get_gpio( spi_dev_gpio_subsrdy ) == SPI_DEV_GPIOLEVEL_HIGH )
	{
		spi_main_send_signal(SPI_MAIN_MSG_SEND);
		return;
	}


	// check SRDY state
	if ( spi_dev_get_gpio( spi_dev_gpio_srdy ) == SPI_DEV_GPIOLEVEL_HIGH )
	{
		spi_main_send_signal(SPI_MAIN_MSG_SEND);
		return;
	}

#ifdef SPI_FEATURE_CONT_SEND
	if ( spi_main_state == SPI_MAIN_STATE_TX_MORE )
	{
		spi_main_state =  SPI_MAIN_STATE_TX_WAIT;
	}
	else
#endif
	{
		// change state SPI_MAIN_STATE_IDLE to SPI_MAIN_STATE_TX_WAIT
		spi_main_state =  SPI_MAIN_STATE_TX_WAIT;

#ifdef SPI_GUARANTEE_MRDY_GAP		  
		mrdy_high_time_save = spi_os_get_tick();
		if( check_mrdy_time_gap(mrdy_low_time_save,mrdy_high_time_save))
		{
			spi_os_loop_delay(MRDY_GUARANTEE_MRDY_LOOP_COUNT);
		}
#endif	

		// MRDY set high 
		spi_dev_set_gpio(spi_dev_gpio_mrdy, SPI_DEV_GPIOLEVEL_HIGH);


		i=0;
		// check SUBSRDY state
		while( spi_dev_get_gpio( spi_dev_gpio_subsrdy ) == SPI_DEV_GPIOLEVEL_LOW )
		{
			if (i == 0)
			{
				spi_os_sleep(1);				
				i++;
				continue;
			}
			else
			 spi_os_sleep(MRDY_GUARANTEE_MRDY_TIME_SLEEP);

			 i++;
			if( i > MRDY_GUARANTEE_MRDY_TIME_SLEEP * 20)
		 	{
				SPI_OS_TRACE(("=== spi Fail to receiving SUBSRDY CONF:spi_main_state=[%d] \n",(int)spi_main_state));

				// MRDY set low
				spi_dev_set_gpio( spi_dev_gpio_mrdy, SPI_DEV_GPIOLEVEL_LOW );
#ifdef SPI_GUARANTEE_MRDY_GAP
				mrdy_low_time_save = spi_os_get_tick();		
#endif
				// change state SPI_MAIN_STATE_TX_WAIT to SPI_MAIN_STATE_IDLE
				if ( spi_main_state != SPI_MAIN_STATE_START 
					&& spi_main_state != SPI_MAIN_STATE_END 
					&& spi_main_state != SPI_MAIN_STATE_INIT )
				{
					spi_main_state =  SPI_MAIN_STATE_IDLE;
					_start_data_send();
				}
				return;
		 	}
		}
		if ( spi_main_state != SPI_MAIN_STATE_START 
			&& spi_main_state != SPI_MAIN_STATE_END 
			&& spi_main_state != SPI_MAIN_STATE_INIT )
		{
			_start_packet_tx_send();
		}
	}
#endif

	return;
}


static void _start_packet_tx_send(void)
{
#ifdef SPI_FEATURE_MASTER
	char * spi_packet_buf = NULL;
	unsigned long 	flags;

	// change state SPI_MAIN_STATE_TX_WAIT to SPI_MAIN_STATE_TX_SENDING
	spi_main_state =  SPI_MAIN_STATE_TX_SENDING;

	// prepare and send spi packet 
	//spi_os_memset( (void *)spi_packet_buf, SPI_MASTER_TX, SPI_DEV_MAX_PACKET_SIZE );
	
#ifdef SPI_FEATURE_CONT_SEND
	if ( ((SPI_DATA_PACKET_HEADER_T *)spi_packet_buf)->more == 1 )
	{
		spi_main_continuous_send++;
		if ( spi_main_continuous_send >= SPI_MAIN_CONT_SEND_AP )
		{
			spi_main_continuous_send = 0;
			((SPI_DATA_PACKET_HEADER_T *)spi_packet_buf)->more = 0;
		}
	}
	else 
		spi_main_continuous_send = 0;
#endif

	spi_packet_buf = gspi_data_packet_buf;
	spi_os_memset(spi_packet_buf, 0, SPI_DEV_MAX_PACKET_SIZE);

	spi_data_prepare_tx_packet( spi_packet_buf );
	if ( spi_dev_send( (void *)spi_packet_buf, SPI_DEV_MAX_PACKET_SIZE ) != 0 )
	{
		// TODO: save failed packet : back data to each queue
	}
	
#ifdef SPI_FEATURE_CONT_SEND
	if ( spi_main_continuous_send > 0 )
	{
		spi_main_state = SPI_MAIN_STATE_TX_MORE;
	}
	else
#endif
	{	/*	spin_lock_irqsave(&spi_lock, flags); */
		
		spi_main_state =  SPI_MAIN_STATE_TX_TERMINATE;
		
		// MRDY set low
		spi_dev_set_gpio( spi_dev_gpio_mrdy, SPI_DEV_GPIOLEVEL_LOW );
#ifdef SPI_GUARANTEE_MRDY_GAP
       	mrdy_low_time_save = spi_os_get_tick();		
#endif
		// SubMRDY set high 
		//spi_dev_set_gpio( spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_HIGH);

		// change state SPI_MAIN_STATE_TX_SENDING to SPI_MAIN_STATE_IDLE
		spi_main_state =  SPI_MAIN_STATE_IDLE;
		_start_data_send();
			/* spin_unlock_irqrestore(&spi_lock, flags); */	}
#endif
}


static void _start_packet_receive(void)
{
#ifndef SPI_FEATURE_DMA
	char * spi_packet_buf = NULL;
	SPI_DATA_PACKET_HEADER_T	spi_receive_packet_header = {0, };
#endif
#if defined SPI_FEATURE_MASTER
	unsigned long 	flags;
	int i;
#endif

#ifdef SPI_FEATURE_SLAVE

	if ( spi_dev_get_gpio( spi_dev_gpio_mrdy ) == SPI_DEV_GPIOLEVEL_LOW )
	{
		return;
	}

	// change state SPI_MAIN_STATE_IDLE to SPI_MAIN_STATE_RX_WAIT
	spi_main_state =  SPI_MAIN_STATE_RX_WAIT;

#ifdef SPI_FEATURE_DMA
	spi_os_memset( spi_rx_buf, SPI_SLAVE_RX, SPI_DEV_MAX_PACKET_SIZE );

#ifdef SPI_FEATURE_SC8800G
	SCI_SPI_EnableDeepSleep (DISABLE_DEEP_SLEEP);
#endif

	spi_dev_receive( (void *) spi_rx_buf, SPI_DEV_MAX_PACKET_SIZE );
	
	// SubSRDY set high
	spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_HIGH);
#else
	// receive SPI packet
	//spi_os_memset( spi_packet_buf, SPI_SLAVE_RX, SPI_DEV_MAX_PACKET_SIZE );

	// SRDY set high
	spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_HIGH);

	if ( spi_dev_receive( (void *) spi_packet_buf, SPI_DEV_MAX_PACKET_SIZE ) == 0)
	{
		// parsing SPI packet
		spi_os_memcpy( &spi_receive_packet_header, spi_packet_buf, SPI_DATA_PACKET_HEADER_SIZE );
		if ( spi_data_parsing_rx_packet( (void *) spi_packet_buf, spi_receive_packet_header.current_data_size ) > 0 )
		{
		// call function for send data to IPC, RAW, RFS
		spi_send_msg_to_app();
	}
	}

	spi_main_state =  SPI_MAIN_STATE_RX_TERMINATE;

	// SRDY set low
	spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);

	// change state SPI_MAIN_STATE_RX_WAIT to SPI_MAIN_STATE_IDLE
	spi_main_state =  SPI_MAIN_STATE_IDLE;

	// Finish receive routine. check tx queue and send data
	_start_data_send();
#endif	
	
#elif defined SPI_FEATURE_MASTER
#ifdef SPI_FEATURE_CONT_SEND
	spi_main_continuous_send = 0;
#endif

	if( !wake_lock_active( &spi_wakelock ) )
	{
		return;
	}

	if ( spi_dev_get_gpio( spi_dev_gpio_srdy ) == SPI_DEV_GPIOLEVEL_LOW )
	{
		return;
	}

	/* spin_lock_irqsave(&spi_lock, flags); */

	spi_main_state =  SPI_MAIN_STATE_RX_WAIT;

	
	spi_dev_set_gpio( spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_HIGH);


	/*
	spin_unlock_irqrestore(&spi_lock, flags);  */

	i=0;
	// check SUBSRDY state
	while( spi_dev_get_gpio( spi_dev_gpio_subsrdy ) == SPI_DEV_GPIOLEVEL_LOW )
	{

		if (i == 0)
		{
			spi_os_sleep(1);				
			i++;
			continue;
		}
		else
		 spi_os_sleep(MRDY_GUARANTEE_MRDY_TIME_SLEEP);

		 i++;
		if( i > MRDY_GUARANTEE_MRDY_TIME_SLEEP * 20)
	 	{
			SPI_OS_ERROR(("[SPI] ERROR (Failed MASTER  RX : %d/%d)\n", i, MRDY_GUARANTEE_MRDY_TIME_SLEEP * 20));
			if ( spi_main_state != SPI_MAIN_STATE_START 
				&& spi_main_state != SPI_MAIN_STATE_END 
				&& spi_main_state != SPI_MAIN_STATE_INIT )
			{
				spi_dev_set_gpio( spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_LOW);

				// change state SPI_MAIN_STATE_RX_WAIT to SPI_MAIN_STATE_IDLE
				spi_main_state =  SPI_MAIN_STATE_IDLE;
			}
			return;
	 	}
	}

	if ( spi_main_state == SPI_MAIN_STATE_START 
		|| spi_main_state == SPI_MAIN_STATE_END 
		|| spi_main_state == SPI_MAIN_STATE_INIT )
		return;

	spi_packet_buf = gspi_data_packet_buf;
	spi_os_memset(spi_packet_buf, 0, SPI_DEV_MAX_PACKET_SIZE);

	if ( spi_dev_receive( (void *) spi_packet_buf, SPI_DEV_MAX_PACKET_SIZE ) == 0 )
	{
		// parsing SPI packet
		spi_os_memcpy( &spi_receive_packet_header, spi_packet_buf, SPI_DATA_PACKET_HEADER_SIZE );

		if ( spi_data_parsing_rx_packet( (void *) spi_packet_buf, spi_receive_packet_header.current_data_size ) > 0 )
		{
			// call function for send data to IPC, RAW, RFS
			//spi_os_get_tick_by_log("_start_packet_receive");
#ifdef SPI_FEATURE_CONT_SEND		
			if ( spi_receive_packet_header.more == 1 )
				spi_main_state = SPI_MAIN_STATE_RX_MORE;
#endif
			spi_send_msg_to_app();
		}
	}

#ifdef SPI_FEATURE_CONT_SEND		
	if ( spi_main_state != SPI_MAIN_STATE_RX_MORE )
#endif
	{
		spi_main_state =  SPI_MAIN_STATE_RX_TERMINATE;
		
		// SubMRDY set low
		spi_dev_set_gpio( spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_LOW);

		// change state SPI_MAIN_STATE_RX_WAIT to SPI_MAIN_STATE_IDLE
		spi_main_state =  SPI_MAIN_STATE_IDLE;
		_start_data_send();
	}
#endif

	return;
}


#ifdef SPI_FEATURE_DMA
void _start_packet_tx_callback( void )
{
	SPI_OS_TRACE_MID(("spi _start_packet_tx_callback\n"));
	spi_main_send_signalfront( SPI_MAIN_MSG_PACKET_SEND );
}

void _start_packet_send_process( void )
{
	SPI_OS_TRACE_MID(("spi _start_packet_send_process\n"));

#ifdef SPI_FEATURE_SC8800G
	SCI_SPI_EnableDeepSleep (ENABLE_DEEP_SLEEP);
#endif

#ifdef SPI_FEATURE_CONT_SEND	
	if ( spi_main_continuous_send > 0 )
	{
		spi_main_state =  SPI_MAIN_STATE_TX_MORE;

		// SRDY set low
		spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);
	}
	else
#endif
	{
		spi_main_state =  SPI_MAIN_STATE_TX_TERMINATE;

		// SUBSRDY set low
		spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);

		// SRDY set low
		spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);

		// change state SPI_MAIN_STATE_TX_WAIT to SPI_MAIN_STATE_IDLE
		spi_main_state =  SPI_MAIN_STATE_IDLE;

		_start_data_send();	
	}
}


void _start_packet_rx_callback( void )
{
	SPI_DATA_PACKET_HEADER_T	spi_receive_packet_header = {0, };

	SPI_OS_TRACE_MID(("spi _start_packet_rx_callback\n"));

	// parsing SPI packet
	spi_os_memcpy( &spi_receive_packet_header, spi_rx_buf, SPI_DATA_PACKET_HEADER_SIZE );

	if ( spi_data_parsing_rx_packet( (void *) spi_rx_buf, spi_receive_packet_header.current_data_size ) > 0 )
	{
		// call function for send data to IPC, RAW, RFS
		//spi_os_get_tick_by_log("_start_packet_rx_callback");
#ifdef SPI_FEATURE_CONT_SEND		
		if ( spi_receive_packet_header.more == 1 )
			spi_main_state = SPI_MAIN_STATE_RX_MORE;
#endif
			
		spi_main_send_signalfront( SPI_MAIN_MSG_PACKET_RECEIVE );
	}
	else
	{
		spi_main_state = SPI_MAIN_STATE_RX_TERMINATE;
		// SUBSRDY set low
		spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);
		// SRDY set low
		spi_dev_set_gpio(spi_dev_gpio_srdy, SPI_DEV_GPIOLEVEL_LOW);
		
		spi_main_state =  SPI_MAIN_STATE_IDLE;
		
		_start_data_send();
	}
}


static void _start_packet_receive_process(void)
{
	spi_send_msg_to_app();

#ifdef SPI_FEATURE_CONT_SEND		
	if ( spi_main_state != SPI_MAIN_STATE_RX_MORE )
#endif
	spi_main_state = SPI_MAIN_STATE_RX_TERMINATE;

	// SubSRDY set low
	spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);

#ifdef SPI_FEATURE_SC8800G
	SCI_SPI_EnableDeepSleep (ENABLE_DEEP_SLEEP);
#endif

	// change state SPI_MAIN_STATE_RX_WAIT to SPI_MAIN_STATE_IDLE

#ifdef SPI_FEATURE_CONT_SEND		
	if ( spi_main_state != SPI_MAIN_STATE_RX_MORE )
#endif
		spi_main_state =  SPI_MAIN_STATE_IDLE;

#ifdef SPI_FEATURE_CONT_SEND		
	if ( spi_main_state == SPI_MAIN_STATE_RX_MORE )
		_start_packet_receive();
	else
#endif
		// Finish receive routine. check tx queue and send data
		_start_data_send();
}
#endif


/*=====================================
//Description	create task
=====================================*/
void spi_main_init(void * data)
{
	SPI_OS_TRACE(("spi_main_init \n"));
	
	spi_main_state =  SPI_MAIN_STATE_START;

#ifdef SPI_FEATURE_SC8800G
#define SPI_STACK_SIZE        1024*16          /* Task stack size */
#define SPI_TASK_PRIORITY   TX_SYSTEM_NORMALE// higher than ipc, TX_SYSTEM_HIGH//(SCI_PRIORITY_HIGHEST)
#define SPI_QUEUE_SIZE        80

	if ( spi_main_thread_id == SCI_INVALID_BLOCK_ID )
	{
		spi_main_thread_id = SCI_CreateAppThread("T_SPI_TASK",
		                                      "Q_SPI_TASK",
		                                      spi_main,
		                                      0,
		                                      0,
		                                      SPI_STACK_SIZE,
		                                      SPI_QUEUE_SIZE,
		                                      SPI_TASK_PRIORITY,
		                                      SCI_PREEMPT,
		                                      SCI_AUTO_START);
	}

	/*check if task create is successful*/
	if ( spi_main_thread_id != SCI_INVALID_BLOCK_ID )
	{
		SPI_OS_TRACE(("[SPI] Create SPI task with ID: 0x%x\n", spi_main_thread_id));
	}
	else
	{
		SPI_OS_ERROR(("[SPI] Create failed SPI task\n"));
		SCI_ASSERT(0);
	}
#undef SPI_STACK_SIZE
#undef SPI_TASK_PRIORITY
#undef SPI_QUEUE_SIZE
#elif defined SPI_FEATURE_OMAP4430
{
	struct ipc_spi_platform_data * pdata;

	pdata = (struct ipc_spi_platform_data *) data;	

	spi_dev_init(pdata);

	spi_dev_reigster_irq_handler (spi_dev_gpio_srdy, SPI_DEV_IRQ_TRIGGER_RISING, spi_main_srdy_rising_handler, "spi_srdy_rising", 0);
	spi_dev_reigster_irq_handler (spi_dev_gpio_subsrdy, SPI_DEV_IRQ_TRIGGER_RISING, spi_main_subsrdy_rising_handler, "spi_subsrdy_rising", 0);	

	wake_lock_init(&spi_wakelock, WAKE_LOCK_SUSPEND, "samsung-spiwakelock");
    
}
#endif	
}


#if defined SPI_FEATURE_OMAP4430
static void spi_main_process(struct work_struct *work)
#else
static void spi_main_process()
#endif
{
	SPI_OS_MSG_T *	signal;
	int				signal_code;

#ifdef SPI_FEATURE_SC8800G
	signal = (SPI_OS_MSG_T *)SCI_GetSignal(spi_main_thread_id);
	signal_code = signal->SignalCode;
#elif defined SPI_FEATURE_OMAP4430
	struct spi_work *spi_wq = container_of(work, struct spi_work, work);
	signal_code = spi_wq->signal_code;
#endif

	// It could be run by remained queue work when cp restart.
	// So, SPI_MAIN_STATE_START should be checked.
	if ( spi_main_state == SPI_MAIN_STATE_END
	||	spi_main_state == SPI_MAIN_STATE_START)
	{
#ifdef SPI_FEATURE_SC8800G
		spi_os_free(signal);
#elif defined SPI_FEATURE_OMAP4430
		spi_os_free(spi_wq);
#endif
		return;
	}

	switch (signal_code)
	{
		case SPI_MAIN_MSG_SEND :
			if ( spi_main_state == SPI_MAIN_STATE_IDLE 
#ifdef SPI_FEATURE_CONT_SEND
				|| spi_main_state == SPI_MAIN_STATE_TX_MORE 
#endif
				)
				_start_packet_tx(  );
			
			break;
			
		case SPI_MAIN_MSG_PACKET_SEND :
#ifdef SPI_FEATURE_SC8800G	
#ifdef SPI_FEATURE_DMA
			_start_packet_send_process( );
#endif
#elif defined SPI_FEATURE_OMAP4430
			_start_packet_tx_send(  );
#endif

			break;

		case SPI_MAIN_MSG_RECEIVE:

			if ( spi_main_state == SPI_MAIN_STATE_IDLE 
				|| spi_main_state == SPI_MAIN_STATE_RX_TERMINATE 
				|| spi_main_state == SPI_MAIN_STATE_TX_TERMINATE
#ifdef SPI_FEATURE_SLAVE
				|| spi_main_state == SPI_MAIN_STATE_TX_START
#endif
#ifdef SPI_FEATURE_CONT_SEND		
				|| spi_main_state == SPI_MAIN_STATE_RX_MORE
#endif
			)
				_start_packet_receive(  );
			break;				

#ifdef SPI_FEATURE_DMA
		case SPI_MAIN_MSG_PACKET_RECEIVE :
			_start_packet_receive_process(  );
			break;
#endif

		// Receive data from IPC, RAW, RFS. It need analyze and save to tx queue
		case SPI_MAIN_MSG_IPC_SEND :
		case SPI_MAIN_MSG_RAW_SEND :
		case SPI_MAIN_MSG_RFS_SEND :
#ifndef SPI_FEATURE_OMAP4430
			spi_receive_msg_from_app(signal);
#endif

			// start send data during SPI_MAIN_STATE_IDLE state
			if ( spi_main_state == SPI_MAIN_STATE_IDLE )
				_start_data_send();
			break;
			
		default:
			SPI_OS_ERROR(("No signal_code case in spi_main [%d]\n", signal_code));
	}

#ifdef SPI_FEATURE_SC8800G	
	spi_os_free(signal);
#elif defined SPI_FEATURE_OMAP4430
	spi_os_free(spi_wq);
	if( wake_lock_active( &spi_wakelock ) ) {
		wake_unlock(&spi_wakelock);
		SPI_OS_TRACE_MID(( "[%s](%d) spi_wakelock unlocked .\n",__func__, __LINE__ ));
	}
#endif
}


/*=====================================
//Description	main task
=====================================*/
#ifdef SPI_FEATURE_SC8800G	
BOOLEAN s_enter_firsttime=FALSE;
extern void GPIO_PROD_SetPhoneActive(BOOLEAN mode);
#endif
void spi_main(unsigned long argc, void* argv)
{
#ifdef SPI_FEATURE_SC8800G	
	// device init
	spi_dev_init( NULL );

	// regist ISR handler
	spi_dev_reigster_irq_handler (spi_dev_gpio_mrdy, SPI_DEV_IRQ_TRIGGER_RISING, GPIO_MrdyIntHandler, "spi_mrdy_rising", 0);
	spi_dev_reigster_irq_handler (spi_dev_gpio_submrdy, SPI_DEV_IRQ_TRIGGER_RISING, GPIO_SubMrdyIntHandler, "spi_submrdy_rising", 0);	
#endif

	// queue init
	spi_data_queue_init();

	SPI_OS_TRACE(("spi_main %x\n", (unsigned int)argv));

#ifdef SPI_FEATURE_SLAVE
	spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_HIGH);
	do
	{
		spi_os_sleep(20);
	}while(spi_dev_get_gpio(spi_dev_gpio_submrdy)==SPI_DEV_GPIOLEVEL_LOW);
	spi_os_sleep(100);
	spi_dev_set_gpio(spi_dev_gpio_subsrdy, SPI_DEV_GPIOLEVEL_LOW);
	spi_main_state = SPI_MAIN_STATE_IDLE;
#ifdef SPI_FEATURE_SC8800G	
	ipc_set_sigs(IPC_SPI_INITIALIZED_SIG);
	
	if ( s_enter_firsttime == FALSE )
	{
		GPIO_PROD_SetPhoneActive(TRUE);
		s_enter_firsttime = TRUE;
	}
#endif
#elif defined SPI_FEATURE_MASTER
	spi_dev_set_gpio(spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_HIGH);
	do
	{
		spi_os_sleep(20);
	}while(spi_dev_get_gpio(spi_dev_gpio_subsrdy)==SPI_DEV_GPIOLEVEL_LOW);
	if ( spi_is_restart )
		spi_os_sleep(100);
	spi_dev_set_gpio(spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_LOW);
	spi_main_state = SPI_MAIN_STATE_IDLE;
	spi_is_restart = 0;
#endif

#ifdef SPI_FEATURE_SC8800G	
	while (1)
	{
		spi_main_process();
	}
#endif
}

#ifdef SPI_FEATURE_MASTER
void spi_set_restart( void )
{
	SPI_OS_TRACE(("[SPI] spi_set_restart\n"));
	
	spi_dev_set_gpio(spi_dev_gpio_submrdy, SPI_DEV_GPIOLEVEL_LOW);
	
	spi_main_state = SPI_MAIN_STATE_END;

	spi_is_restart = 1;

	flush_workqueue(suspend_work_queue);

	spi_data_queue_destroy();

	spi_dev_destroy();

#ifdef SPI_FEATURE_OMAP4430	
	if( wake_lock_active( &spi_wakelock ) ) {
		wake_unlock(&spi_wakelock);
		SPI_OS_TRACE_MID(( "[%s](%d) spi_wakelock unlocked .\n",__func__, __LINE__ ));
	}	 
		wake_lock_destroy(&spi_wakelock);
#endif
}
#endif
