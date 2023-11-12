#include <stddef.h>

#define array_invalid_offset ((size_t)-1)
#define array_uninitialized_offset ((size_t)-2)

#define array_define(value_type, size)          \
    typedef struct {                            \
        const size_t item_position;             \
        size_t next_free_item;                  \
        value_type value;                       \
    } array_node_##value_type;                  \
    typedef struct {                            \
        const size_t item_count;                \
        const size_t item_size;                 \
        size_t first_free_item;                 \
        size_t last_free_item;                  \
        array_node_##value_type items[size];    \
    } array_##size##_##value_type

#define array_type(value_type, size)    \
    array_##size##_##value_type

#define array_create(value_type, size)                  \
    {                                                   \
        .item_count = size,                             \
        .item_size = sizeof(array_node_##value_type),   \
        .first_free_item = array_uninitialized_offset,  \
        .last_free_item = array_uninitialized_offset    \
    }

void *array_value_from_position(void *array, size_t position);
void *array_lock_value(void *array);
void array_free_value(void *array, void *value);
size_t array_value_position(void *value);
