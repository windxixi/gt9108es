#ifndef __SPI_MAIN_H__
#define __SPI_MAIN_H__

#define SPI_FEATURE_OMAP4430
//#define SPI_FEATURE_SC8800G
#define SPI_FEATURE_MASTER 
//#define SPI_FEATURE_SLAVE

#define SPI_FEATURE_NOLOG
//#define SPI_FEATURE_DEBUG
//#define SPI_FEATURE_CONT_SEND
//#define SPI_FEATURE_DMA
#define SPI_GUARANTEE_MRDY_GAP

//#define SPI_FEATURE_TEST_SCENARIO		SPI_TEST_SCENARIO_MASTER_SENDING
//#define SPI_FEATURE_TEST_SCENARIO		SPI_TEST_SCENARIO_SLAVE_SENDING
//#define SPI_FEATURE_TEST_SCENARIO		SPI_TEST_SCENARIO_PHYSICAL
//#define SPI_FEATURE_TEST_SCENARIO		SPI_TEST_SCENARIO_NONE
//#define SPI_FEATURE_TEST_DURATION		1000

#define SPI_SLAVE_TX 0
#define SPI_SLAVE_RX 0
#define SPI_MASTER_TX 0
#define SPI_MASTER_RX 0

#include "spi_os.h"

#ifdef SPI_FEATURE_SC8800G
#include "os_api.h"
#endif

typedef enum {
	SPI_MAIN_MSG_BASE,
	SPI_MAIN_MSG_SEND,
	SPI_MAIN_MSG_RECEIVE,
	SPI_MAIN_MSG_PACKET_SEND,
	SPI_MAIN_MSG_PACKET_RECEIVE,
	SPI_MAIN_MSG_IPC_SEND, 	//: IPC sends message to SPI
	SPI_MAIN_MSG_RAW_SEND, 	//: RAW sends message to SPI
	SPI_MAIN_MSG_RFS_SEND	//: RFS sends message to SPI	
} SPI_MAIN_MSG_T;

typedef enum
{
	SPI_MAIN_STATE_START,			//: before init complete
	SPI_MAIN_STATE_INIT,			//: initialising
	SPI_MAIN_STATE_IDLE, 			//: suspend. Waiting for event
	SPI_MAIN_STATE_TX_START, 		//: state to start tx. Become from idle
	SPI_MAIN_STATE_TX_CONFIRM,
	SPI_MAIN_STATE_TX_WAIT,		//: (in case of master)
									//: wait srdy rising interrupt to check slave preparing
									//: (in case of slave)
									//: wait submrdy rising interrupt to check sync complete
	SPI_MAIN_STATE_TX_SENDING,	// : tx data sending 
	SPI_MAIN_STATE_TX_TERMINATE,	//: 
	SPI_MAIN_STATE_TX_MORE,
	SPI_MAIN_STATE_RX_WAIT,		//: in case of slave, wait submrdy rising interrupt to 
									//: check sync complete then it starts to read buffer
	SPI_MAIN_STATE_RX_MORE,
	SPI_MAIN_STATE_RX_TERMINATE,	//: 
	SPI_MAIN_STATE_END			//: spi task is stopped.
}SPI_MAIN_STATE_T;


extern SPI_MAIN_STATE_T spi_main_state;
extern SPI_OS_THREAD_ID_T spi_main_thread_id;

extern void spi_main(unsigned long argc, void* argv);
extern void spi_main_init(void * data);
extern void spi_main_send_signal(SPI_MAIN_MSG_T  spi_sigs);
extern void spi_main_send_signalfront(SPI_MAIN_MSG_T  spi_sigs);
#ifdef SPI_FEATURE_MASTER
extern void spi_set_restart( void );
#endif
#endif
