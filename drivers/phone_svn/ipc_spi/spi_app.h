#ifndef _SPI_APP_H_
#define _SPI_APP_H_

#include "spi_main.h"
#include "spi_data.h"
#include "spi_os.h"

extern int app_send_data_to_spi  (SPI_MAIN_MSG_T type, void *data, unsigned int length );
extern void spi_receive_msg_from_app  ( SPI_OS_MSG_T * msg );
extern void spi_send_msg_to_app (void);
extern int   spi_is_ready ( void );
#endif
