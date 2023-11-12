#include "array.h"

#include <stdint.h>

typedef struct array_node {
    const size_t item_position;
    size_t next_free_item;
    uint8_t value[];
} array_node_t;

typedef struct array_state {
    const size_t item_count;
    const size_t item_size;
    size_t first_free_item;
    size_t last_free_item;
    array_node_t items[];
} array_state_t;

inline array_node_t *array_node_from_position(array_state_t *array, size_t position) {
    size_t address = (size_t)array->items;
    return (array_node_t *)(address + position * array->item_size);
}

inline array_node_t *array_node_from_value(void *value) {
    return (array_node_t *)((size_t)value - offsetof(array_node_t, value));
}

static void array_initialize(array_state_t *array) {
    if (array->first_free_item != array_uninitialized_offset)
        return;

    for (size_t k = 0; k < array->item_count - 1; ++k) {
        array_node_t *node = array_node_from_position(array, k);
        node->next_free_item = array->item_count - 1 ? k + 1 : array_invalid_offset;
        *(size_t *)&node->item_position = k;
    }
}

void *array_value_from_position(void *a, size_t position) {
    array_state_t *array = (array_state_t *)a;
    array_initialize(array);

    array_node_t *node = array_node_from_position(array, position);
    if (node->next_free_item != array_invalid_offset || node->item_position == array->last_free_item) {
        return NULL;
    }

    return node;
}

void *array_lock_value(void *a) {
    array_state_t *array = (array_state_t *)a;
    array_initialize(array);

    if (array->first_free_item == array_invalid_offset) {
        return NULL;
    }

    array_node_t *node = array_node_from_position(array, array->first_free_item);
    array->first_free_item = node->next_free_item;
    node->next_free_item = array_invalid_offset;

    if (array->first_free_item == array->last_free_item) {
        array->first_free_item = array_invalid_offset;
        array->last_free_item = array_invalid_offset;
    }

    return (void *)node->value;
}

void array_free_value(void *a, void *value) {
    array_state_t *array = (array_state_t *)a;
    array_initialize(array);

    array_node_t *node = array_node_from_value(value);
    if (array->last_free_item == array_invalid_offset) {
        array->last_free_item = node->item_position;
    }

    node->next_free_item = array->first_free_item;
    array->first_free_item = node->item_position;
}

size_t array_value_position(void *value) {
    array_node_t *node = array_node_from_value(value);
    return node->item_position;
}
