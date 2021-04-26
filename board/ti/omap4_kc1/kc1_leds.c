#include <asm/byteorder.h>
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include <twl6030.h>

#define LED_PWM1ON           0xBA
#define LED_PWM1OFF          0xBB
#define LED_PWM2ON           0xBD
#define LED_PWM2OFF          0xBE
#define TWL6030_TOGGLE3      0x92

int setled(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[], uint led_on, uint led_off)
{
    int ret = 0;
    unsigned char brightness;
    unsigned char data;

    if (i2c_get_bus_num() != 0) {
        if (i2c_set_bus_num(0) == 0) { /* configure I2C1 */
            printf("====   Change to bus 1.  ====\n");
        }
    }

    if (argc < 2) {
        printf("Usage[%d]:\n%s\n", argc, cmdtp->usage);
        return 1;
    }

    data = 0xff;
    i2c_write(TWL6030_CHIP_PWM, led_on, 1, &data, 1);
    data = 0x7f;
    i2c_write(TWL6030_CHIP_PWM, led_off, 1, &data, 1);
    //select_bus(0,400);

    brightness = simple_strtol(argv[1], NULL, 16);
    if (brightness > 1) {
        if (brightness == 255)
            brightness = 0x7f;
        else
            brightness = (~(brightness/2)) & 0x7f;

        if (led_on == LED_PWM2ON)
            data = 0x30;
        else
            data = 0x06;
        i2c_write(TWL6030_CHIP_PWM, TWL6030_TOGGLE3, 1, &data, 1);
        i2c_write(TWL6030_CHIP_PWM, led_on, 1, &brightness, 1);
    }
    else if (brightness <= 1) {
        if (led_on == LED_PWM2ON)
            data = 0x08;
        else
            data = 0x01;
        i2c_write(TWL6030_CHIP_PWM, TWL6030_TOGGLE3, 1, &data, 1);
        if (led_on == LED_PWM2ON)
            data = 0x38;
        else
            data = 0x07;
        i2c_write(TWL6030_CHIP_PWM, TWL6030_TOGGLE3, 1, &data, 1);
    }

    return ret;
}

int do_setamberled(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
    debug("** SET ORANGE LED: %s %s\n", argv[0], argv[1]);
    return setled(cmdtp, flag, argc, argv, LED_PWM2ON, LED_PWM2OFF);
}

int do_setgreenled(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
    debug("** SET GREEN LED: %s %s\n", argv[0], argv[1]);
    return setled(cmdtp, flag, argc, argv, LED_PWM1ON, LED_PWM1OFF);
}

U_BOOT_CMD( setgreenled, 2, 0, do_setgreenled, "setgreenled - set green led brightness.", NULL );
U_BOOT_CMD( setamberled, 2, 0, do_setamberled, "setamberled - set amber led brightness.", NULL );

