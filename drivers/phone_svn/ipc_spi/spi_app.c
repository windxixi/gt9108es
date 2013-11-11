#include "spi_main.h"
#include "spi_app.h"
#include "spi_os.h"
#include "spi_data.h"
#include "spi_test.h"

#ifdef SPI_FEATURE_SC8800G
#include "ipcdef.h"
#endif


#ifndef SPI_FEATURE_OMAP4430
extern char * gspi_data_sync;
#endif

static unsigned int _pack_spi_data (SPI_DATA_TYPE_T type, void * buf, void * data, unsigned int length);
static void * _unpack_spi_data (void * buf, void * data, unsigned int length, unsigned int * pmsg_length);
static void * _unpack_spi_data_ipc (void * buf, void * data, unsigned int length, unsigned int * pmsg_length);


/**********************************************************
Prototype	void spi_send_data (SPI_APP_MESSAGE_T type, void *data, unsigned int length )
Type			function
Description	Send message to spi task
			Ex) IPC calls "spi_app_send_data" for sending data to spi
Param input	type 	: type of message data
         		data 	: address of sending data
			length 	: length of data
Return value	(none)

***********************************************************/
int app_send_data_to_spi  (SPI_MAIN_MSG_T type, void *data, unsigned int length )
{
#ifdef SPI_FEATURE_SC8800G
	{
		SPI_OS_MSG_T *msg;
		SPI_OS_TRACE_MID(("app_send_data_to_spi : SPI_MAIN_MSG type [%d] state [%d] length %d\n",type,spi_main_state, length));

		if ( spi_is_ready() == 0 )
			return 0;

		msg = (SPI_OS_MSG_T *)spi_os_malloc(sizeof(SPI_OS_MSG_T));
		msg->SignalCode = type;
		msg->SignalSize = sizeof(SPI_OS_MSG_T);
		msg->spi_packet_length = length;
		msg->spi_pkt = data;

		if (SCI_SUCCESS != SCI_SendSignal(msg, spi_main_thread_id))
		{
			spi_os_free(msg);
			SPI_OS_ASSERT(("Send Msg Error in spi_send_data: msg = SPI_TASK_SIG_SEND Fail\n"));
			return 0;
		}
	}
#elif defined SPI_FEATURE_OMAP4430
	if (data != 0)
		spi_os_free(data);
#endif
	return 1;
}


/**********************************************************
Prototype	void spi_app_receive_msg ( SPI_OS_MSG_T * msg )
Type			function
Description	receive data from spi task message queue and Pack data for spi data.
			Then inqueue the message to spi_data_queue_XXX_tx
Param input	msg	: message received from other task
Return value	(none)
***********************************************************/
void spi_receive_msg_from_app ( SPI_OS_MSG_T * msg )
{
	SPI_MAIN_MSG_T type;
	SPI_DATA_QUEUE_TYPE_T q_type;
	SPI_DATA_TYPE_T mux_type;
	unsigned int in_length = 0, out_length = 0;
	void * in_buffer = 0;
	void * out_buffer = 0;

#ifdef SPI_FEATURE_SC8800G
	type = (SPI_MAIN_MSG_T)((SPI_OS_MSG_T *)msg)->SignalCode;
	in_length = ((SPI_OS_MSG_T *)msg)->spi_packet_length;
	in_buffer = ((SPI_OS_MSG_T *)msg)->spi_pkt;
#elif defined SPI_FEATURE_OMAP4430
	type = msg->signal_code;
	in_length = msg->data_length;
	in_buffer = msg->data;
#endif

	switch (type)
	{
		case SPI_MAIN_MSG_IPC_SEND:
			q_type = SPI_DATA_QUEUE_TYPE_IPC_TX;
			mux_type = SPI_DATA_MUX_IPC; 
			break;
		case SPI_MAIN_MSG_RAW_SEND:
			q_type = SPI_DATA_QUEUE_TYPE_RAW_TX;
			mux_type = SPI_DATA_MUX_RAW; 
			break;
		case SPI_MAIN_MSG_RFS_SEND:
			q_type = SPI_DATA_QUEUE_TYPE_RFS_TX; 	
			mux_type = SPI_DATA_MUX_RFS; 
			break;
		default:
			SPI_OS_ASSERT(("spi_app_receive_msg Unknown Message Fail\n"));
			return;			
	}

	out_buffer = spi_os_malloc(in_length+SPI_DATA_HEADER_SIZE); 
	out_length = _pack_spi_data(mux_type, out_buffer, in_buffer, in_length);

	if (spi_data_inqueue(&spi_data_queue_info[q_type], out_buffer, out_length)==0)
	{
		SPI_OS_ASSERT(("spi_app_receive_msg inqueue[type:%d] Fail",q_type));
	}

	spi_os_free(in_buffer);
	spi_os_free(out_buffer);
}


/**********************************************************
Prototype	void spi_send_msg ( void )
Type			function
Description	Dequeue a spi data from spi_data_queue_XXX_rx 
			Unpack the spi data for ipc, raw or rfs data 
			Send msg to other task until that all queues are empty
			CP use this functions for other task as below
			IPC : ipc_cmd_send_queue
			RAW : data_send_queue
			RFS : rfs_send_queue
Param input	(none)
Return value	(none)
***********************************************************/
void spi_send_msg_to_app(void)
{
#ifdef SPI_FEATURE_SC8800G
	void *msg	= NULL; 		
	unsigned int data_length, msg_length = 0;

	extern void ipc_cmd_send_queue(ipc_message_type *cmd_ptr);	
	extern void data_send_queue(ipc_data_type *cmd_ptr);		
	extern void rfs_send_queue(ipc_rfs_type *cmd_ptr);		
#endif
	
#ifdef SPI_FEATURE_OMAP4430
	extern void ipc_spi_make_data_interrupt( u32 cmd,  struct ipc_spi *od );
	#define MB_VALID					0x0080
	#define MB_DATA( x )				( MB_VALID | x )
	#define MBD_SEND_FMT			0x0002
	#define MBD_SEND_RAW			0x0001
	#define MBD_SEND_RFS			0x0100

	u32 int_cmd = 0;
	struct ipc_spi * od = NULL;
	extern struct ipc_spi *ipc_spi;

	od = ipc_spi;
#endif

#ifdef SPI_FEATURE_OMAP4430
	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_IPC_RX) == 0)
	{
		int_cmd = MB_DATA( MBD_SEND_FMT );
		ipc_spi_make_data_interrupt(int_cmd, od);
	}

	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_RAW_RX) == 0)
	{
		int_cmd = MB_DATA( MBD_SEND_RAW );
		ipc_spi_make_data_interrupt(int_cmd, od);
	}

	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_RFS_RX) == 0)
	{
		int_cmd = MB_DATA( MBD_SEND_RFS );
		ipc_spi_make_data_interrupt(int_cmd, od);
	}
#else
	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_IPC_RX) == 0)
	{
		do
		{	
			spi_os_memset( gspi_data_sync, 0, SPI_DEV_MAX_PACKET_SIZE );
			data_length = spi_data_dequeue(&spi_data_queue_info[SPI_DATA_QUEUE_TYPE_IPC_RX], gspi_data_sync );
			
			if (data_length > 0)
			{
				msg = _unpack_spi_data_ipc(msg, gspi_data_sync, data_length, &msg_length);
#ifdef SPI_FEATURE_SC8800G
				ipc_cmd_send_queue(msg);
#endif
			}
		} while (data_length > 0);
	}

	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_RAW_RX) == 0)
	{
		do
		{	
			spi_os_memset( gspi_data_sync, 0, SPI_DEV_MAX_PACKET_SIZE );
			data_length = spi_data_dequeue(&spi_data_queue_info[SPI_DATA_QUEUE_TYPE_RAW_RX], gspi_data_sync );

			if (data_length > 0)
			{
				msg = _unpack_spi_data(msg, gspi_data_sync, data_length, &msg_length);
				
#ifdef SPI_FEATURE_SC8800G
				data_send_queue(msg);
#endif
			}
		} while (data_length > 0);
	}

	if (spi_data_queue_is_empty(SPI_DATA_QUEUE_TYPE_RFS_RX) == 0)
	{
		do
		{	
			spi_os_memset( gspi_data_sync, 0, SPI_DEV_MAX_PACKET_SIZE );
			data_length = spi_data_dequeue(&spi_data_queue_info[SPI_DATA_QUEUE_TYPE_RFS_RX], gspi_data_sync );

			if (data_length > 0)
			{
				msg = _unpack_spi_data(msg, gspi_data_sync, data_length, &msg_length);
				
#ifdef SPI_FEATURE_SC8800G
				rfs_send_queue(msg);
#endif
			}
		} while (data_length > 0);
	}
#endif
}


/**********************************************************
Prototype	unsigned int _pack_spi_data (SPI_DATA_TYPE_T type,void * buf, void * data, unsigned int length)
Type			static function
Description	pack data for spi
Param input	type		: type of data type
			buf 		: address of buffer to be saved
			data		: address of data to pack
			length	: length of input data
Return value	length of packed data
***********************************************************/
static unsigned int _pack_spi_data (SPI_DATA_TYPE_T type, void * buf, void * data, unsigned int length)
{
	char * spi_packet	 = NULL;
	unsigned int out_length = 0;

	spi_packet = (char *) buf;
	
	spi_os_memset( (char *)spi_packet, 0x00, (unsigned int)length);

#ifdef SPI_FEATURE_OMAP4430
 	spi_os_memset ( (char *) spi_packet, (unsigned char)SPI_DATA_BOF, SPI_DATA_BOF_SIZE );
	spi_os_memcpy( (char *) spi_packet + SPI_DATA_BOF_SIZE, data, length );
	spi_os_memset ( (char *) spi_packet + SPI_DATA_BOF_SIZE + length, (unsigned char)SPI_DATA_EOF, SPI_DATA_EOF_SIZE );

	out_length = SPI_DATA_BOF_SIZE + length + SPI_DATA_EOF_SIZE;
#else
	spi_os_memcpy( (char *) spi_packet , (char *)&type, SPI_DATA_MUX_SIZE );
	spi_os_memcpy( (char *) spi_packet + SPI_DATA_LENGTH_OFFSET, (char *)&length, SPI_DATA_LENGTH_SIZE );
 	spi_os_memset ( (char *) spi_packet + SPI_DATA_BOF_OFFSET, (unsigned char)SPI_DATA_BOF, SPI_DATA_BOF_SIZE );
	spi_os_memcpy( (char *) spi_packet + SPI_DATA_DATA_OFFSET, data, length );
	spi_os_memset ( (char *) spi_packet + SPI_DATA_EOF_OFFSET(length), (unsigned char)SPI_DATA_EOF, SPI_DATA_EOF_SIZE );

	out_length = SPI_DATA_HEADER_SIZE + length;
#endif	

	return out_length;
}


/**********************************************************
Prototype	unsigned int _unpack_spi_data (void * buf, void * data, unsigned int length)
Type			static function
Description	unpack a spi data for raw or rfs data
Param input	buf 		: address of buffer to be saved
			data 	: address of spi data to unpack
			length	: length of input data
Return value	length of data unpacked
***********************************************************/
static void * _unpack_spi_data (void * buf, void * data, unsigned int length, unsigned int * pmsg_length)
{
	unsigned int msg_length;
	char * pdata = NULL;

	pdata = data;

	msg_length = length - SPI_DATA_HEADER_SIZE;

	buf = (char*)spi_os_malloc(msg_length);
	spi_os_memset(buf, 0x00, msg_length);
	spi_os_memcpy(buf, pdata + SPI_DATA_HEADER_SIZE_FRONT, msg_length );

	* pmsg_length = msg_length;
	return buf;
}


/**********************************************************
Prototype	unsigned int _unpack_spi_data_ipc (void * buf, void * data, unsigned int length)
Type			static function
Description	unpack a spi data for ipc
Param input	buf 		: address of buffer to be saved
			data 	: address of spi data to unpack
			length	: length of input data
Return value	length of data unpacked
***********************************************************/
static void * _unpack_spi_data_ipc (void * buf, void * data, unsigned int length, unsigned int * pmsg_length)
{
	unsigned int msg_length;
	char * pdata = NULL;

	pdata = data;

	msg_length = length - SPI_DATA_HEADER_SIZE - SPI_DATA_IPC_INNER_HEADER_SIZE;

	buf = (char*)spi_os_malloc(msg_length);
	spi_os_memset(buf, 0x00, msg_length);
	spi_os_memcpy(buf, pdata + SPI_DATA_HEADER_SIZE_FRONT + SPI_DATA_IPC_INNER_HEADER_SIZE, msg_length );

	* pmsg_length = msg_length;
	return buf;
}


/**********************************************************
Prototype	int spi_app_ready ( void )
Type			function
Description	check if spi initialization is done. Decide it as spi_main_state.
Param input	(none)
Return value	1	 : spi initialization is done.
			0	 : spi initialization is not done.
***********************************************************/
int spi_is_ready ( void )
{
	if ((spi_main_state==SPI_MAIN_STATE_START)||(spi_main_state==SPI_MAIN_STATE_END))
		return 0;

	return 1;
}

