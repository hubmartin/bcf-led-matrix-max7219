#include <application.h>

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

bc_gfx_t my_gfx;
bc_gfx_t *gfx = &my_gfx;

#define LED_MODULES_COUNT 4
#define CHAIN_MAX_LEN 8

uint8_t framebuffer[LED_MODULES_COUNT * 8 * 8];

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_set_mode(&led, BC_LED_MODE_TOGGLE);
        bc_radio_pub_push_button(0);
    }
}

void led_matrix_send_data_multiple(uint8_t command, uint8_t *value, uint8_t chain_length)
{

    uint8_t src[2 * CHAIN_MAX_LEN];

    if(chain_length > CHAIN_MAX_LEN)
    {
        chain_length = CHAIN_MAX_LEN;
    }

    int i;

    for (i = 0; i < chain_length; i++)
    {
        // Same command for all devices
        src[i * 2 + 0] = command;
        // Different display data for each device
        src[i * 2 + 1] = value[i];
    }

    bc_spi_transfer(src, NULL, 2 * chain_length);
}

void led_matrix_send_command_multiple(uint8_t command, uint8_t value, uint8_t chain_length)
{
    uint8_t src[2 * CHAIN_MAX_LEN];

    if(chain_length > CHAIN_MAX_LEN)
    {
        chain_length = CHAIN_MAX_LEN;
    }

    int i;

    for (i = 0; i < chain_length; i++)
    {
        // Same command for all devices
        src[i * 2 + 0] = command;
        // Different display data for each device
        src[i * 2 + 1] = value;
    }

    bc_spi_transfer(src, NULL, 2 * chain_length);
}

void led_matrix_update(void *self)
{
    int8_t line;

    for(line = 0; line < 8; line++)
    {
        led_matrix_send_data_multiple(line + 1, (uint8_t*)&framebuffer[line * LED_MODULES_COUNT], LED_MODULES_COUNT);
    }
}

void led_matrix_send_command(uint8_t command, uint8_t value)
{
    uint8_t src[2];

    src[0] = command;
    src[1] = value;

    bc_spi_transfer(src, NULL, 2);
}

void led_matrix_init(void)
{
    bc_spi_init(BC_SPI_SPEED_1_MHZ, BC_SPI_MODE_0);

    while(!bc_spi_is_ready());

    // No decode mode
    led_matrix_send_command_multiple(0x09, 0x00, LED_MODULES_COUNT);
    // Disable test
    led_matrix_send_command_multiple(0x0F, 0x00, LED_MODULES_COUNT);
    // Scan libit - 8 sloupců
    led_matrix_send_command_multiple(0x0B, 0x07, LED_MODULES_COUNT);
    // Disable shutdown
    led_matrix_send_command_multiple(0x0C, 0x01, LED_MODULES_COUNT);
    // intensity
    led_matrix_send_command_multiple(0x0A, 0x02, LED_MODULES_COUNT);

}

bool led_matrix_is_ready(void *param)
{
    return true;
}

void led_matrix_clear(void *param)
{
    memset(framebuffer, 0x00, sizeof(framebuffer));
}

void led_matrix_draw_pixel(void *param, uint8_t x, uint8_t y, uint32_t enabled)
{

    uint8_t sub = LED_MODULES_COUNT-1;

    if(enabled)
    {
        framebuffer[(sub - (x / 8)) + (8-y) * LED_MODULES_COUNT] |= 1 << (x % 8);
    }
    else
    {
        framebuffer[(sub - (x / 8)) + (8-y) * LED_MODULES_COUNT] &= ~(1 << (x % 8));
    }
}

bc_gfx_caps_t led_matrix_get_caps(bc_ls013b7dh03_t *self)
{
    (void) self;

    static const bc_gfx_caps_t caps = { .width = 8 * LED_MODULES_COUNT, .height = 9 }; // Height is 9 because we shift text 1 pixel up

    return caps;
}

const bc_gfx_driver_t *led_matrix_get_driver(void)
{
    static const bc_gfx_driver_t driver =
    {
        .is_ready = (bool (*)(void *)) led_matrix_is_ready,
        .clear = (void (*)(void *)) led_matrix_clear,
        .draw_pixel = (void (*)(void *, int, int, uint32_t)) led_matrix_draw_pixel,
        .update = (bool (*)(void *)) led_matrix_update,
        .get_caps = (bc_gfx_caps_t (*)(void *)) led_matrix_get_caps
    };

    return &driver;
}

void led_matrix_string_set(uint64_t *id, const char *topic, void *value, void *param)
{
    bc_gfx_clear(gfx);
    bc_gfx_draw_string(gfx, -1, 0, (char *) value, 1);
    bc_gfx_update(gfx);
}

void led_matrix_intensity_set(uint64_t *id, const char *topic, void *value, void *param)
{
    int intensity = *(int *) value;

    if (intensity < 0 || intensity > 15)
    {
        return;
    }

    // set intensity
    led_matrix_send_command_multiple(0x0A, intensity, LED_MODULES_COUNT);
}

// subscribe table, format: topic, expect payload type, callback, user param
static const bc_radio_sub_t subs[] = {
    // state/set
    {"led-matrix/-/text/set", BC_RADIO_SUB_PT_STRING, led_matrix_string_set, (void *)0},
    {"led-matrix/-/intensity/set", BC_RADIO_SUB_PT_INT, led_matrix_intensity_set, (void *)0}
};

void application_init(void)
{
    // Initialize radio
    bc_radio_init(BC_RADIO_MODE_NODE_LISTENING);
    bc_radio_set_subs((bc_radio_sub_t *) subs, sizeof(subs)/sizeof(bc_radio_sub_t));

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    bc_radio_pairing_request("led-matrix", VERSION);
    bc_led_pulse(&led, 2000);

    led_matrix_init();
    bc_gfx_init(gfx, NULL, led_matrix_get_driver());

    bc_gfx_set_font(gfx, &bc_font_ubuntu_11);
    bc_gfx_draw_string(gfx, -1, 0, "BigClwn", 1);
    bc_gfx_update(gfx);
}
