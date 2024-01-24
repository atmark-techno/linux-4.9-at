/*
 * Copyright (C) 2018 Atmark Techno, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#define GPIO_BMIC_REG_OFFSET	1

#define BMIC_GPIO_MAJOR_VERSION(v)	((v >> 8) & 0xff)
#define BMIC_GPIO_MINOR_VERSION(v)	(v & 0xff)

#define BMIC_GPIO_VER1_0	0x0100 /* version 1.0 */
#define BMIC_GPIO_VER2_0	0x0200 /* version 2.0 */

#define REG_VERSION	0x01
#define REG_VALUE	0x02
#define REG_DIRECTION	0x03

struct gpio_bmic {
	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	struct mutex lock;
	u8 reg_output;
	s32 version;
};

static int gpio_bmic_gpio_direction_output(struct gpio_chip *gc,
					   unsigned off, int val)
{
	struct gpio_bmic *gpio;
	u8 output;
	int ret = 0;
	bool atomic_context = in_atomic() || irqs_disabled();

	gpio = gpiochip_get_data(gc);

	if (!atomic_context)
		mutex_lock(&gpio->lock);

	output = gpio->reg_output;
	if (val)
		output |= (1 << (off + GPIO_BMIC_REG_OFFSET));
	else
		output &= ~(1 << (off + GPIO_BMIC_REG_OFFSET));

	if (output == gpio->reg_output)
		goto exit;

	ret = i2c_smbus_write_byte_data(gpio->client, REG_VALUE, output);
	if (ret < 0)
		goto exit;

	gpio->reg_output = output;

exit:
	if (!atomic_context)
		mutex_unlock(&gpio->lock);
	return ret;
}

static int gpio_bmic_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct gpio_bmic *gpio;
	u8 output;
	bool atomic_context = in_atomic() || irqs_disabled();

	gpio = gpiochip_get_data(gc);

	if (!atomic_context)
		mutex_lock(&gpio->lock);
	output = gpio->reg_output;
	if (!atomic_context)
		mutex_unlock(&gpio->lock);

	return !!(output & (1 << (off + GPIO_BMIC_REG_OFFSET)));
}

static void gpio_bmic_gpio_set_value(struct gpio_chip *gc,
				     unsigned off, int val)
{
	gpio_bmic_gpio_direction_output(gc, off, val);
}

static int gpio_bmic_setup_gpio(struct gpio_bmic *gpio)
{
	struct gpio_chip *gc;
	int ret;

	gc = &gpio->gpio_chip;

	gc->direction_output = gpio_bmic_gpio_direction_output;
	gc->get = gpio_bmic_gpio_get_value;
	gc->set = gpio_bmic_gpio_set_value;
	gc->can_sleep = true;

	gc->base = -1;
	if (gpio->version < BMIC_GPIO_VER2_0)
		gc->ngpio = 2;
	else
		gc->ngpio = 3;
	gc->label = gpio->client->name;
	gc->parent = &gpio->client->dev;
	gc->of_node = gc->parent->of_node;
	gc->owner = THIS_MODULE;

	ret = i2c_smbus_read_byte_data(gpio->client, REG_VALUE);
	if (ret < 0)
		return ret;

	gpio->reg_output = ret;

	return 0;
}

static int gpio_bmic_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct gpio_bmic *gpio;
	int ver;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	ver = i2c_smbus_read_word_data(client, REG_VERSION);
	dev_info(&client->dev, "version: %d.%d\n",
		 BMIC_GPIO_MAJOR_VERSION(ver),
		 BMIC_GPIO_MINOR_VERSION(ver));

	gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->client = client;
	gpio->version = ver;

	mutex_init(&gpio->lock);

	ret = gpio_bmic_setup_gpio(gpio);
	if (ret)
		return ret;

	ret = devm_gpiochip_add_data(&client->dev, &gpio->gpio_chip, gpio);
	if (ret)
		return ret;

	i2c_set_clientdata(client, gpio);

	return 0;
}

static int gpio_bmic_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id gpio_bmic_id[] = {
	{ "bmic_gpio", 0, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gpio_bmic_id);

#ifdef CONFIG_OF
static const struct of_device_id gpio_bmic_dt_ids[] = {
	{ .compatible = "at,bmic_gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_bmic_dt_ids);
#endif

static struct i2c_driver gpio_bmic_driver = {
	.driver = {
		.name	= "gpio_bmic",
		.of_match_table = of_match_ptr(gpio_bmic_dt_ids),
	},
	.probe		= gpio_bmic_probe,
	.remove		= gpio_bmic_remove,
	.id_table	= gpio_bmic_id,
};

static int __init gpio_bmic_init(void)
{
	return i2c_add_driver(&gpio_bmic_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(gpio_bmic_init);

static void __exit gpio_bmic_exit(void)
{
	i2c_del_driver(&gpio_bmic_driver);
}
module_exit(gpio_bmic_exit);

MODULE_AUTHOR("Atmark Techno, Inc.");
MODULE_DESCRIPTION("Atmark Techno BMIC GPIO driver");
MODULE_LICENSE("GPL v2");
