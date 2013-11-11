#include "spi_main.h"
#include "spi_os.h"

#ifdef SPI_FEATURE_SC8800G
#include <os_api.h>
#elif defined SPI_FEATURE_OMAP4430
#include <linux/vmalloc.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/time.h>
#endif



/*====================================
//Description	allocate memory with kmalloc
//Param input	length	: length of size
//Return value	0	: fail
//			(other)	: address of memory allocated
====================================*/
void * spi_os_malloc ( unsigned int length )
{
	if (length <= 0)
	{
		SPI_OS_ERROR (("spi_os_malloc fail : length is 0\n")); 
		return 0;
	}

#ifdef SPI_FEATURE_OMAP4430
	return kmalloc(length, GFP_ATOMIC);
#elif defined SPI_FEATURE_SC8800G
	return SCI_ALLOC_APP(length);
#endif
}

/*====================================
//Description	allocate memory with vmalloc
//Param input	length	: length of size
//Return value	0	: fail
//			(other) : address of memory allocated
====================================*/
void * spi_os_vmalloc ( unsigned int length )
{
	if (length <= 0)
	{
		SPI_OS_ERROR (("spi_os_malloc fail : length is 0\n")); 
		return 0;
	}

#ifdef SPI_FEATURE_OMAP4430
		return vmalloc(length);
#elif defined SPI_FEATURE_SC8800G
		return SCI_ALLOC_APP(length);
#endif
}

/*====================================
//Description	free memory with kfree
//Param input	addr	: address of memory to be released
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_free ( void * addr )
{
	if (addr == 0)
	{
		SPI_OS_ERROR (("spi_os_free fail : addr is 0\n"));
		return 0;
	}

#ifdef SPI_FEATURE_OMAP4430
	kfree( addr );
	return 1;
#elif defined SPI_FEATURE_SC8800G
	SCI_FREE(addr);
	return 1;
#endif
}


/*====================================
//Description	free memory with vfree
//Param input	addr	: address of memory to be released
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_vfree ( void * addr )
{
	if (addr == 0)
	{
		SPI_OS_ERROR (("spi_os_free fail : addr is 0\n"));
		return 0;
	}

#ifdef SPI_FEATURE_OMAP4430
	vfree( addr );
	return 1;
#elif defined SPI_FEATURE_SC8800G
	SCI_FREE(addr);
	return 1;
#endif
}


/*====================================
//Description	copy memory
//Param input	dest	: address of memory to be save
//				src	: address of memory to copy
//				length	: length of memory to copy
//Return value	0	: fail
//				1	: success

====================================*/
int spi_os_memcpy ( void * dest, void * src, unsigned int length )
{
	if (dest == 0 || src == 0)
	{
		SPI_OS_ERROR (("spi_os_memcpy fail\n"));
		return 0;
	}
	
#ifdef SPI_FEATURE_OMAP4430
	return memcpy(dest, src, length);
#elif defined SPI_FEATURE_SC8800G
	SCI_MEMCPY(dest, src, length);
	return 1;
#endif
}


/*====================================
//Description	set memory as parameter
//Param input	addr	: address of memory to be set
//				value	: value to set
//				length	: length of memory to be set
//Return value	0	: fail
//				1	: success
====================================*/
void * spi_os_memset ( void * addr, int value, unsigned int length )
{	
	if (addr == 0)
	{
		SPI_OS_ERROR (("spi_os_memcpy fail\n"));
		return 0;
	}
	
#ifdef SPI_FEATURE_OMAP4430
	return memset(addr, value, length);
#elif defined SPI_FEATURE_SC8800G
	SCI_MEMSET(addr, value, length);
	return NULL;
#endif
}


/*====================================
//Description	create mutex
//Param input	name	: name of mutex
//Return value	id of mutex created
====================================*/
SPI_OS_MUTEX_T spi_os_create_mutex( char * name, unsigned int priority_inherit )
{
	if (name == 0)
	{
		SPI_OS_ERROR (("spi_os_memcpy fail\n"));
		return 0;
	}
	
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_CreateMutex( name, priority_inherit);
#endif
}


/*====================================
//Description	delete mutex
//Prototype	int spi_os_delete_mutex ( SPI_OS_MUTEX_T pmutex )
//Param input	sem	: id of mutex to delete
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_delete_mutex ( SPI_OS_MUTEX_T pmutex )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_DeleteMutex( pmutex );
#endif
}


/*====================================
//Description	delete mutex
//Prototype	int spi_os_acquire_mutex ( SPI_OS_MUTEX_T pmutex )
//Param input	sem	: id of mutex to acquire
//Return value	0	: fail
//				1	: success
====================================*/
int  spi_os_acquire_mutex ( SPI_OS_MUTEX_T pmutex, unsigned int wait  )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_GetMutex( pmutex, wait );
#endif
}


/*====================================
//Description	release mutex
//Prototype	int spi_os_release_mutex ( SPI_OS_MUTEX_T pmutex )
//Param input	sem	: id of mutex to release
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_release_mutex (SPI_OS_MUTEX_T pmutex )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_PutMutex(pmutex);
#endif
}


/*====================================
//Description	create semaphore
//Param input	name	: name of semaphore
//Return value	id of semaphore created
====================================*/
SPI_OS_SEM_T spi_os_create_sem ( char * name, unsigned int init_count)
{
	if (name == 0)
	{
		SPI_OS_ERROR (("spi_os_memcpy fail\n"));
		return 0;
	}
	
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_CreateSemaphore(name, init_count);
#endif
}


/*====================================
//Description	delete semaphore
//Prototype	int spi_os_delete_sem ( SPI_OS_SEM_T sem )
//Param input	sem	: id of semaphore to delete
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_delete_sem ( SPI_OS_SEM_T sem )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_DeleteSemaphore(sem);
#endif
}


/*====================================
//Description	acquire semaphore
//Prototype	int spi_os_acquire_sem (SPI_OS_SEM_T sem )
//Param input	sem	: id of semaphore to acquire
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_acquire_sem (SPI_OS_SEM_T sem, unsigned int wait )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_GetSemaphore(sem, wait);
#endif
}


/*====================================
//Description	release semaphore
//Param input	sem	: id of semaphore to acquire
//Return value	0	: fail
//				1	: success
====================================*/
int spi_os_release_sem (SPI_OS_SEM_T sem )
{
#ifdef SPI_FEATURE_OMAP4430
	return 0; // TBD
#elif defined SPI_FEATURE_SC8800G
	return SCI_PutSemaphore(sem);
#endif
}


/*====================================
//Description	sleep os
//Param input	msec	: time to sleep
//Return value	(none)
====================================*/
void spi_os_sleep ( unsigned long msec )
{
	if (msec <= 0)
	{
		SPI_OS_ERROR (("spi_os_sleep fail\n"));
		return;
	}

#ifdef SPI_FEATURE_OMAP4430
	msleep(msec);
#elif defined SPI_FEATURE_SC8800G
	SCI_Sleep(msec);
#endif
}

void spi_os_loop_delay ( unsigned long cnt )
{
	volatile unsigned int timeout;
	timeout = 0;
	while(++ timeout < cnt);
}

void * spi_os_create_timer ( char * name, SPI_OS_TIMER_T callback, int param, unsigned long duration)
{
	if (name == 0 || callback == 0 || param <= 0 || duration <= 0)
	{
		SPI_OS_ERROR (("spi_os_create_timer fail\n"));
		return 0;
	}

#ifdef SPI_FEATURE_OMAP4430
	{
		struct timer_list tm;

		init_timer (&tm);

		tm.expires = jiffies + ((duration * HZ) / 1000);
		tm.data = (unsigned long) param;
		tm.function = callback;

		return &tm;
	}	
#elif defined SPI_FEATURE_SC8800G
	return SCI_CreateTimer (name, callback, (unsigned long)param, duration, 0);
#endif
}


int spi_os_start_timer (void * timer, SPI_OS_TIMER_T callback, int param, unsigned long duration)
{
#ifdef SPI_FEATURE_OMAP4430
	add_timer ((struct timer_list *) timer);
	return 1;
#elif defined SPI_FEATURE_SC8800G
	{
		unsigned int value;

		if (SCI_IsTimerActive((SCI_TIMER_PTR) timer))
		{
			SCI_DeactiveTimer ((SCI_TIMER_PTR)timer);
		}

		SCI_ChangeTimer((SCI_TIMER_PTR)timer, callback, duration);
		value = SCI_ActiveTimer ((SCI_TIMER_PTR) timer);
		return (value == SCI_SUCCESS) ? 1 : 0;
	}		
#endif
}


int spi_os_stop_timer (void * timer)
{
#ifdef SPI_FEATURE_OMAP4430
	{
		int value;
		value = del_timer ((struct timer_list *) timer);
		return value;
	}	
#elif defined SPI_FEATURE_SC8800G
	{
		unsigned int value;
		value = SCI_DeactiveTimer ((SCI_TIMER_PTR) timer);
		return (value == SCI_SUCCESS) ? 1 : 0;
	}		
#endif
}


int spi_os_delete_timer (void * timer)
{
#ifdef SPI_FEATURE_OMAP4430
	return 1;
#elif defined SPI_FEATURE_SC8800G
	{
		unsigned int value;
		value = SCI_DeleteTimer ((SCI_TIMER_PTR) timer);
		return (value == SCI_SUCCESS) ? 1 : 0;
	}		
#endif
}


unsigned long spi_os_get_tick (void)
{
	unsigned long tick = 0;

#ifdef SPI_FEATURE_OMAP4430
	tick = jiffies_to_msecs(jiffies);
#else
	tick = SCI_GetTickCount();
#endif
	return tick;
}


void spi_os_get_tick_by_log (char * name)
{
	SPI_OS_TRACE (("%s tick %lu ms\n", name, spi_os_get_tick()));
}


void spi_os_trace_dump (char * name, void * data, int length)
{
#ifdef SPI_FEATURE_DEBUG
	#define SPI_OS_TRACE_DUMP_PER_LINE  	16
	#define SPI_OS_TRACE_MAX_LINE			8
	#define SPI_OS_TRACE_HALF_LINE			(SPI_OS_TRACE_MAX_LINE/2)
	#define SPI_OS_TRACE_MAX_DUMP_SIZE 	(SPI_OS_TRACE_DUMP_PER_LINE*SPI_OS_TRACE_MAX_LINE)

	int i, lines = 0, halflinemode = 0;
	char buf[SPI_OS_TRACE_DUMP_PER_LINE*3+1];
	char * pdata;
	char ch;

	SPI_OS_TRACE_MID(("spi_os_trace_dump (%s length[%d])\n",name, length));

	spi_os_memset(buf, 0x00, sizeof(buf));

	if (length > SPI_OS_TRACE_MAX_DUMP_SIZE)
		halflinemode = 1;

	pdata = data;
	for (i=0;i<length;i++)
	{
		if ((i!=0)&&((i%SPI_OS_TRACE_DUMP_PER_LINE)==0))
		{
			buf[SPI_OS_TRACE_DUMP_PER_LINE*3] = 0;
			SPI_OS_TRACE_MID(("%s\n",buf));
			spi_os_memset(buf, 0x00, sizeof(buf));
			lines++;
			if (SPI_OS_TRACE_HALF_LINE == lines && halflinemode == 1)
			{
				SPI_OS_TRACE_MID((" ...... \n"));
				pdata += (length - SPI_OS_TRACE_MAX_DUMP_SIZE);
				i += (length - SPI_OS_TRACE_MAX_DUMP_SIZE);
			}
		}

		ch = (*pdata&0xF0)>>4;
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3] = ((ch > 9) ? (ch-10 + 'A') : (ch +  '0'));
		ch = (*pdata&0x0F);
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3+1] = ((ch > 9) ? (ch-10 + 'A') : (ch +  '0'));
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3+2] = 0x20;
		pdata++;
	}

	if (buf[0]!='\0')
		SPI_OS_TRACE_MID(("%s\n",buf));

	#undef SPI_OS_TRACE_DUMP_PER_LINE
	#undef SPI_OS_TRACE_MAX_LINE
	#undef SPI_OS_TRACE_HALF_LINE
	#undef SPI_OS_TRACE_MAX_DUMP_SIZE
#endif
}


void spi_os_trace_dump_low (char * name, void * data, int length)
{
	#define SPI_OS_TRACE_DUMP_PER_LINE  	16

	int i = 0;
	char buf[SPI_OS_TRACE_DUMP_PER_LINE*3+1] = {0,};
	char * pdata = NULL;
	char ch = 0;

	SPI_OS_ERROR(("[SPI] spi_os_trace_dump_low (%s length[%d])\n",name, length));

	spi_os_memset(buf, 0x00, sizeof(buf));

	if (length > SPI_OS_TRACE_DUMP_PER_LINE)
		length = SPI_OS_TRACE_DUMP_PER_LINE;

	pdata = data;
	for (i=0;i<length;i++)
	{
		ch = (*pdata&0xF0)>>4;
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3] = ((ch > 9) ? (ch-10 + 'A') : (ch +  '0'));
		ch = (*pdata&0x0F);
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3+1] = ((ch > 9) ? (ch-10 + 'A') : (ch +  '0'));
		buf[(i%SPI_OS_TRACE_DUMP_PER_LINE)*3+2] = 0x20;
		pdata++;
	}

	if (buf[0]!='\0')
		SPI_OS_ERROR(("%s\n",buf));

	#undef SPI_OS_TRACE_DUMP_PER_LINE
}

