/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "log.h"
#include "vfio_pci.h"
#include "vfio_pci_irq.h"

struct irq_event {
	int efd;
	void *cb_arg;
	void (*callback)(void *cb_arg);
};

struct vfio_pci_irq {
	pthread_t thread;
	bool running;
	int epoll_fd;
	uint16_t nb_cbs;
	int max_events;
	struct irq_event *events;
};

static struct vfio_pci_irq *irq_handle;

static void
process_interrupts(struct epoll_event *ep_events, int n)
{
	struct irq_event *event;
	int bytes_read, i;
	uint64_t cntr;

	for (i = 0; i < n; i++) {
		if (!(ep_events[i].events & EPOLLIN)) {
			log_write(LOG_ERR, "Unexpected event received, events=%x\n",
				  ep_events[i].events);
			continue;
		}

		event = ep_events[i].data.ptr;

		bytes_read = read(event->efd, &cntr, sizeof(cntr));
		if (bytes_read <= 0) {
			log_write(LOG_ERR, "Failure in reading efd %d, %s\n", event->efd,
				  strerror(errno));
			continue;
		}

		if (!event->callback) {
			log_write(LOG_DEBUG, "No callback registered for efd %d\n", event->efd);
			continue;
		}

		event->callback(event->cb_arg);
	}
}

static void *
irq_handle_thread()
{
	struct epoll_event *ep_events;
	int n;

	ep_events = calloc(irq_handle->max_events, sizeof(struct epoll_event));
	if (!ep_events) {
		log_write(LOG_ERR, "Failed to allocate memory for epoll events\n");
		goto exit;
	}

	while (irq_handle->running) {
		n = epoll_wait(irq_handle->epoll_fd, ep_events, irq_handle->max_events, -1);
		if (n < 0) {
			log_write(LOG_ERR, "epoll_wait failed\n");
			break;
		}

		process_interrupts(ep_events, n);
	}
exit:
	log_write(LOG_DEBUG, "Interrupt handle thread exiting\n");
	free(ep_events);
	return NULL;
}

static int
vfio_pci_irq_init(struct vfio_pci_device *pdev)
{
	irq_handle = calloc(1, sizeof(struct vfio_pci_irq));
	if (!irq_handle) {
		log_write(LOG_ERR, "Failed to allocate memory for interrupt handle\n");
		return -1;
	}

	irq_handle->epoll_fd = epoll_create1(0);
	if (irq_handle->epoll_fd < 0) {
		log_write(LOG_ERR, "Failed to create epoll fd\n");
		goto exit;
	}

	irq_handle->events = calloc(pdev->intr.count, sizeof(struct irq_event));
	if (!irq_handle->events) {
		log_write(LOG_ERR, "Failed to allocate memory for interrupt events\n");
		goto exit;
	}

	irq_handle->max_events = pdev->intr.count;
	irq_handle->running = true;
	if (pthread_create(&irq_handle->thread, NULL, irq_handle_thread, NULL)) {
		log_write(LOG_ERR, "Failed to create interrupt handle thread\n");
		goto exit;
	}

	return 0;
exit:
	if (irq_handle->epoll_fd >= 0)
		close(irq_handle->epoll_fd);
	free(irq_handle->events);
	free(irq_handle);
	irq_handle = NULL;
	return -1;
}

int
vfio_pci_irq_register(struct vfio_pci_device *pdev, uint16_t vec, vfio_pci_irq_cb_t callback,
		      void *cb_arg)
{
	struct epoll_event ev;
	int rc = -1;

	pthread_mutex_lock(&pdev->intr.lock);

	if (vec > pdev->intr.count) {
		log_write(LOG_ERR, "Invalid vector %u\n", vec);
		goto exit;
	}

	if (!callback) {
		log_write(LOG_ERR, "Invalid callback\n");
		goto exit;
	}

	if (!irq_handle && vfio_pci_irq_init(pdev)) {
		log_write(LOG_ERR, "Failed to initialize interrupt callback\n");
		goto exit;
	}

	if (irq_handle->events[vec].callback) {
		log_write(LOG_ERR, "Callback already registered for vector %u\n", vec);
		goto exit;
	}

	if (pdev->intr.efds[vec] < 0) {
		log_write(LOG_ERR, "Interrupt vector %u is not enabled, invalid efd\n", vec);
		goto exit;
	}

	irq_handle->events[vec].callback = callback;
	irq_handle->events[vec].cb_arg = cb_arg;
	irq_handle->events[vec].efd = pdev->intr.efds[vec];
	irq_handle->nb_cbs++;

	ev.events = EPOLLIN;
	ev.data.ptr = &irq_handle->events[vec];
	rc = epoll_ctl(irq_handle->epoll_fd, EPOLL_CTL_ADD, irq_handle->events[vec].efd, &ev);
	if (rc < 0) {
		log_write(LOG_ERR, "Failed to add efd to epoll fd waitlist, %s\n", strerror(errno));
		goto exit;
	}

	log_write(LOG_DEBUG, "Registered interrupt vector %u\n", vec);
exit:
	pthread_mutex_unlock(&pdev->intr.lock);
	return rc;
}

static void
interrupt_thread_stop(__attribute__((unused)) void *data)
{
	irq_handle->running = false;
}

static int
vfio_pci_irq_fini()
{
	struct irq_event event;
	struct epoll_event ev;
	uint64_t data = 1;
	int exit_efd, rc;

	/* Create a dummy efd to break epoll wait in the handler */
	exit_efd = eventfd(0, EFD_NONBLOCK);
	if (exit_efd < 0) {
		log_write(LOG_ERR, "Failed to create exit efd, %s\n", strerror(errno));
		return -1;
	}

	event.efd = exit_efd;
	event.callback = interrupt_thread_stop;

	ev.events = EPOLLIN;
	ev.data.ptr = &event;
	rc = epoll_ctl(irq_handle->epoll_fd, EPOLL_CTL_ADD, exit_efd, &ev);
	if (rc < 0) {
		log_write(LOG_ERR, "Failed to add exit efd to epoll fd waitlist, %s\n",
			  strerror(errno));
		close(exit_efd);
		return -1;
	}

	write(exit_efd, &data, sizeof(data));

	/* Wait for interrupt handler to stop */
	if (pthread_join(irq_handle->thread, NULL)) {
		log_write(LOG_ERR, "Failed to join interrupt handle thread\n");
		close(exit_efd);
		return -1;
	}

	close(exit_efd);
	close(irq_handle->epoll_fd);
	free(irq_handle->events);
	free(irq_handle);
	irq_handle = NULL;

	return 0;
}
int
vfio_pci_irq_unregister(struct vfio_pci_device *pdev, uint16_t vec)
{
	int rc = -1;

	pthread_mutex_lock(&pdev->intr.lock);

	if (vec > pdev->intr.count) {
		log_write(LOG_ERR, "Invalid vector %u\n", vec);
		goto exit;
	}

	if (!irq_handle) {
		log_write(LOG_ERR, "Interrupt handle not initialized\n");
		goto exit;
	}

	if (!irq_handle->events[vec].callback) {
		log_write(LOG_ERR, "No callback registered for vector %u\n", vec);
		goto exit;
	}

	rc = epoll_ctl(irq_handle->epoll_fd, EPOLL_CTL_DEL, irq_handle->events[vec].efd, NULL);
	if (rc < 0) {
		log_write(LOG_ERR, "Failed to remove efd from epoll fd waitlist, %s\n",
			  strerror(errno));
		goto exit;
	}

	memset(&irq_handle->events[vec], 0, sizeof(struct irq_event));
	irq_handle->nb_cbs--;

	if (!irq_handle->nb_cbs && vfio_pci_irq_fini()) {
		log_write(LOG_ERR, "Failed to cleanup IRQ processing\n");
		goto exit;
	}

	log_write(LOG_DEBUG, "Unregistered interrupt vector %u\n", vec);
exit:
	pthread_mutex_unlock(&pdev->intr.lock);
	return rc;
}
