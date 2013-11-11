#ifndef __SPI_OS_H__
#define __SPI_OS_H__

#include "spi_main.h"

#ifdef SPI_FEATURE_SC8800G
#include "sci_types.h"
#include "os_api.h"
#elif defined SPI_FEATURE_OMAP4430
#include <linux/kernel.h>
#include <linux/workqueue.h>
#endif

#ifdef SPI_FEATURE_SC8800G
#define SPI_OS_ASSERT(x) SCI_TraceError##x
#ifdef SPI_FEATURE_NOLOG
#define SPI_OS_TRACE_MID(x)
#define SPI_OS_TRACE(x)
#else
#define SPI_OS_TRACE(x)  SCI_TraceLow##x
#ifdef SPI_FEATURE_DEBUG
#define SPI_OS_TRACE_MID(x)  SCI_TraceLow##x
#else
#define SPI_OS_TRACE_MID(x)
#endif // SPI_FEATURE_DEBUG
#endif // SPI_FEATURE_NOLOG
#define SPI_OS_ERROR(x)  SCI_TraceLow##x
#elif defined SPI_FEATURE_OMAP4430
#define SPI_OS_ASSERT(x) printk x
#ifdef SPI_FEATURE_NOLOG
#define SPI_OS_TRACE_MID(x)
#define SPI_OS_TRACE(x)
#else
#define SPI_OS_TRACE(x)   printk x
#ifdef SPI_FEATURE_DEBUG
#define SPI_OS_TRACE_MID(x)  printk x
#else
#define SPI_OS_TRACE_MID(x)
#endif // SPI_FEATURE_DEBUG
#endif // SPI_FEATURE_NOLOG
#define SPI_OS_ERROR(x)  printk x
#endif

#ifdef SPI_FEATURE_SC8800G

typedef BLOCK_ID SPI_OS_THREAD_ID_T;
typedef struct {
	uint16 SignalCode;
	uint16 SignalSize;
	xSignalHeader  Pre;
	xSignalHeader  Suc;
	BLOCK_ID       Sender;
	void	    *spi_pkt;			// message content
	unsigned int spi_packet_length;
} SPI_OS_MSG_T;
typedef void *SPI_OS_SEM_T;
typedef void *SPI_OS_MUTEX_T;
typedef void (*SPI_OS_TIMER_T)(unsigned long);
// Inherit option
#define SPI_OS_MUTEX_NO_INHERIT 0
#define SPI_OS_MUTEX_INHERIT 1
// Wait option.
#define SPI_OS_MUTEX_NO_WAIT                 0x0
#define SPI_OS_MUTEX_WAIT_FOREVER            0xFFFFFFFF
#define SPI_OS_TIMER_CALLBACK(X)  void X (unsigned long param)
#elif defined SPI_FEATURE_OMAP4430
struct spi_work {
	struct work_struct	work;
	int 		signal_code;
};
typedef int SPI_OS_THREAD_ID_T;
typedef struct {
	unsigned int signal_code;
	unsigned int data_length;
	void * data;
} SPI_OS_MSG_T;
typedef int SPI_OS_SEM_T;
typedef int SPI_OS_MUTEX_T;
typedef void (*SPI_OS_TIMER_T)(unsigned long);
// Inherit option
#define SPI_OS_MUTEX_NO_INHERIT 0
#define SPI_OS_MUTEX_INHERIT 1
// Wait option.
#define SPI_OS_MUTEX_NO_WAIT                 //TBD
#define SPI_OS_MUTEX_WAIT_FOREVER       //TBD
#define SPI_OS_TIMER_CALLBACK(X)  void X (unsigned long param)


#endif

extern void * spi_os_malloc ( unsigned int length );
extern void * spi_os_vmalloc ( unsigned int length );
extern int      spi_os_free ( void * addr );
extern int		spi_os_vfree ( void * addr );
extern int      spi_os_memcpy ( void * dest, void * src, unsigned int length );
extern void * spi_os_memset ( void * addr, int value, unsigned int length );
extern SPI_OS_MUTEX_T spi_os_create_mutex ( char * name, unsigned int priority_inherit);
extern int spi_os_delete_mutex ( SPI_OS_MUTEX_T sem );
extern int spi_os_acquire_mutex (SPI_OS_MUTEX_T sem, unsigned int wait );
extern int spi_os_release_mutex (SPI_OS_MUTEX_T sem );
extern SPI_OS_SEM_T spi_os_create_sem ( char * name, unsigned int init_count);
extern int spi_os_delete_sem ( SPI_OS_SEM_T sem );
extern int spi_os_acquire_sem (SPI_OS_SEM_T sem, unsigned int wait );
extern int spi_os_release_sem (SPI_OS_SEM_T sem );
extern void spi_os_sleep ( unsigned long msec );
extern void spi_os_loop_delay(unsigned long cnt);
extern void * spi_os_create_timer ( char * name, SPI_OS_TIMER_T callback, int param, unsigned long duration);
extern int spi_os_start_timer (void * timer, SPI_OS_TIMER_T callback, int param, unsigned long duration);
extern int      spi_os_stop_timer (void * timer);
extern int      spi_os_delete_timer (void * timer);
extern unsigned long spi_os_get_tick (void);
extern void spi_os_get_tick_by_log (char * name);
extern void spi_os_trace_dump (char * name, void * data, int length);
extern void spi_os_trace_dump_low (char * name, void * data, int length);
#endif
