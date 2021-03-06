/*
 * audio_player.c
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"

#include "audio_player.h"
#include "esp32_fifo.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"

#include "mp3_decoder.h"
#include "controls.h"

#define TAG "audio_player"
#define PRIO_MAD configMAX_PRIORITIES - 2

static player_t *player_instance = NULL;
static component_status_t player_status = UNINITIALIZED;

static int start_decoder_task(player_t *player)
{
    TaskFunction_t task_func;
    char *task_name;
    uint16_t stack_depth;

    ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());

    task_func = mp3_decoder_task;
    task_name = "mp3_decoder_task";
    stack_depth = 8448;

    player->decoder_status = RUNNING;
    if (xTaskCreatePinnedToCore(task_func, task_name, stack_depth, player, PRIO_MAD, NULL, 1) != pdPASS)
    {
        ESP_LOGE(TAG, "ERROR creating decoder task! Out of memory?");
        player->decoder_status = STOPPED;
        return -1;
    }

    ESP_LOGI(TAG, "created decoder task: %s", task_name);

    return 0;
}

static int t;

/* Writes bytes into the FIFO queue, starts decoder task if necessary. */
int audio_stream_consumer(const char *recv_buf, ssize_t bytes_read, void *user_data)
{
    player_t *player = user_data;

    // don't bother consuming bytes if stopped
    if (player->command == CMD_STOP)
    {
        player->decoder_command = CMD_STOP;
        player->command = CMD_NONE;
        return -1;
    }

    if (bytes_read > 0)
    {
        FifoWrite(recv_buf, bytes_read);
    }

    int bytes_in_buf = FifoFill();
    uint8_t fill_level = (bytes_in_buf * 100) / FifoLen();

    // seems 4k is enough to prevent initial buffer underflow
    uint8_t min_fill_lvl = player->buffer_pref == BUF_PREF_FAST ? 20 : 90;
    bool enough_buffer = fill_level > min_fill_lvl;

    if (player->decoder_command != CMD_START && enough_buffer)
    {
        // buffer is filled, start decoder
        player->decoder_command = CMD_START;
    }

    t = (t + 1) & 255;
    if (t == 0)
    {
#if configGENERATE_RUN_TIME_STATS == 1 // use this only for debug since it mess up with the i2s timings
        static char __stats_buffer[1024];
        vTaskGetRunTimeStats(__stats_buffer);
        printf("%s\n", __stats_buffer);
#endif
#ifdef CONFIG_BUFFER_INFO
        ESP_LOGI(TAG, "Buffer fill %u%%, %d bytes RAM left %d", fill_level, bytes_in_buf, esp_get_free_heap_size());
#endif
        player->fill_level = fill_level;
    }

    return 0;
}

void audio_player_init(player_t *player)
{
    player_instance = player;
    player_status = INITIALIZED;
    start_decoder_task(player);
}

void audio_player_destroy()
{
    renderer_destroy();
    player_status = UNINITIALIZED;
}

void audio_player_start()
{
    renderer_start();
    player_status = RUNNING;
}

void audio_player_stop()
{
    renderer_stop();
    player_instance->command = CMD_STOP;
    // player_status = STOPPED;
}

component_status_t get_player_status()
{
    return player_status;
}
