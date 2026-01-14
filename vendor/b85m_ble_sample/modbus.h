#ifndef __MODBUS_H
#define __MODBUS_H


void modbus_uart_init(void);
void uart_dma_send_packet(const unsigned char *data, unsigned int n);
void main_loop_modbus(void);

#endif