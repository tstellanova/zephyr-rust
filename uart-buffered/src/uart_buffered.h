#ifndef __UART_BUFFERED_H__
#define __UART_BUFFERED_H__

#include <zephyr.h>
#include <kernel.h>

struct fifo {
	u16_t write;
	u16_t read;
	u8_t buf[];
};

#define FIFO_DEFINE(name, size)                                                \
	u8_t name[offsetof(struct fifo, buf) + (size)];                        \
	BUILD_ASSERT_MSG(((size) & ((size)-1)) == 0,                           \
			 "fifo size must be a power of 2")

#define FIFO_CAPACITY(name) (sizeof(name) - offsetof(struct fifo, buf))

/*
 * Copyable set of references needed to operate on the fifo. The kernel owns
 * the primary copy in kernel memory which can't be modififed by un-trusted
 * code. This can be copied for access by user space if the thread is granted
 * access to the kernel objects and the fifo memory.
 */
struct fifo_handle {
	struct fifo *fifo;
	/* capacity - 1. Capacity must be power of 2 */
	size_t capacity_mask;
	struct device *device;
	struct k_poll_signal *signal;
};

/* New type to prevent mixing rx and tx functions */
struct uart_buffered_rx_handle {
	struct fifo_handle fifo;
};

/* New type to prevent mixing rx and tx functions */
struct uart_buffered_tx_handle {
	struct fifo_handle fifo;
};

static inline size_t fifo_capacity(struct fifo_handle *fifo)
{
	return fifo->capacity_mask + 1;
}

static inline size_t fifo_used(struct fifo_handle *fifo)
{
	return fifo->fifo->write - fifo->fifo->read;
}

static inline bool fifo_full(struct fifo_handle *fifo)
{
	return fifo_used(fifo) >= fifo_capacity(fifo);
}

static inline bool fifo_empty(struct fifo_handle *fifo)
{
	return fifo_used(fifo) == 0;
}

static inline void fifo_push(struct fifo_handle *fifo, u8_t val)
{
	__ASSERT(!fifo_full(fifo), "push to full fifo");
	fifo->fifo->buf[fifo->fifo->write++ & fifo->capacity_mask] = val;
}

static inline u8_t fifo_peek(struct fifo_handle *fifo)
{
	__ASSERT(!fifo_empty(fifo), "peek from empty fifo");
	return fifo->fifo->buf[fifo->fifo->read & fifo->capacity_mask];
}

static inline u8_t fifo_pop(struct fifo_handle *fifo)
{
	__ASSERT(!fifo_empty(fifo), "pop from empty fifo");
	return fifo->fifo->buf[fifo->fifo->read++ & fifo->capacity_mask];
}

/* Kernel memory storage for kobjects and the kernel's fifo handle */
struct uart_buffered_rx {
	struct fifo_handle fifo;
	struct k_poll_signal signal;
	struct k_timer *const timer;
};

/* Kernel memory storage for kobjects and the kernel's fifo handle */
struct uart_buffered_tx {
	struct fifo_handle fifo;
	struct k_poll_signal signal;
};

struct uart_buffered {
	struct uart_buffered_rx rx;
	struct uart_buffered_tx tx;
};

static inline struct uart_buffered_rx_handle
uart_buffered_rx_handle(struct uart_buffered *uart)
{
	struct uart_buffered_rx_handle handle;

	handle.fifo = uart->rx.fifo;

	return handle;
}

static inline struct uart_buffered_tx_handle
uart_buffered_tx_handle(struct uart_buffered *uart)
{
	struct uart_buffered_tx_handle handle;

	handle.fifo = uart->tx.fifo;

	return handle;
}

#define FIFO_INITIALIZER(fifo_name, signal_name)                               \
	{                                                                      \
		.fifo = (struct fifo *)&fifo_name,                             \
		.capacity_mask = FIFO_CAPACITY(fifo_name) - 1, .device = NULL, \
		.signal = signal_name,                                         \
	}

/* 
 * Devices can't have user data, so this function passes the static fifo
 * pointers for this specific fifo to the generic IRQ function.
 */
#define UART_FIFO_IRQ_DEFINE(name, rx, tx)                                     \
	static void name##_irq(struct device *uart)                            \
	{                                                                      \
		uart_buffered_irq(uart, rx, tx);                               \
	}

#define UART_BUFFERED_DEFINE(name, rx_fifo, tx_fifo)                           \
	K_TIMER_DEFINE(name##_timer, uart_buffered_rx_timeout, NULL);          \
	struct uart_buffered name =                                            \
		{ .rx =                                                        \
			  {                                                    \
				  .fifo = FIFO_INITIALIZER(rx_fifo,            \
							   &name.rx.signal),   \
				  .timer = &name##_timer,                      \
			  },                                                   \
		  .tx = {                                                      \
			  .fifo = FIFO_INITIALIZER(tx_fifo, &name.tx.signal),  \
		  } };                                                         \
	UART_FIFO_IRQ_DEFINE(name, &name.rx, &name.tx)

#define UART_BUFFERED_INIT(name, uart)                                         \
	uart_buffered_init(name, uart, name##_irq)

/* Invoked by macros */
void uart_buffered_rx_timeout(struct k_timer *timer);
void uart_buffered_irq(struct device *uart, struct uart_buffered_rx *rx_fifo,
		       struct uart_buffered_tx *tx_fifo);
void uart_buffered_init(struct uart_buffered *buffered, struct device *uart,
			void (*irq_handler)(struct device *uart));

/* API */
int uart_buffered_write_nb(struct uart_buffered_tx_handle *tx, const u8_t *buf,
			   size_t len);
void uart_buffered_write(struct uart_buffered_tx_handle *tx, const u8_t *buf,
			 size_t len);
int uart_buffered_read_nb(struct uart_buffered_rx_handle *rx, u8_t *buf,
			  size_t len);
size_t uart_buffered_read(struct uart_buffered_rx_handle *rx, u8_t *buf,
			  size_t len);
void uart_buffered_access_grant(struct uart_buffered *uart,
				struct k_thread *thread);

#endif
