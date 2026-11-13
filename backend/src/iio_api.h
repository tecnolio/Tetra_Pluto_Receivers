#ifndef IIO_MIN_H
#define IIO_MIN_H


struct iio_context;
struct iio_device;
struct iio_channel;
struct iio_buffer;

struct iio_context *iio_create_local_context(void);

unsigned int
iio_context_get_devices_count(const struct iio_context *ctx);

struct iio_device *
iio_context_find_device(const struct iio_context *ctx,
                        const char *name);

void
iio_context_destroy(struct iio_context *ctx);

const char *
iio_device_get_name(const struct iio_device *dev);

struct iio_channel *
iio_device_find_channel(const struct iio_device *dev,
                        const char *name,
                        int output);

void
iio_channel_enable(struct iio_channel *chn);

struct iio_buffer *
iio_device_create_buffer(const struct iio_device *dev,
                         size_t samples_count,
                         int cyclic);

ssize_t
iio_buffer_refill(struct iio_buffer *buf);

void *
iio_buffer_first(const struct iio_buffer *buf,
                 const struct iio_channel *chn);

ptrdiff_t
iio_buffer_step(const struct iio_buffer *buf);

void *
iio_buffer_end(const struct iio_buffer *buf);

void
iio_buffer_destroy(struct iio_buffer *buf);

#endif