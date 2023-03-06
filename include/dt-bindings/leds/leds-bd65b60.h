/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides macros for the ROHM BD65B60 device tre§e bindings.
 *
 * Copyright (C) 2023 Bogdan Ionescu <bogdan.ionescu.work+kernel@gmail.com>
 */

#ifndef _DT_BINDINGS_LEDS_BD65B60_H
#define _DT_BINDINGS_LEDS_BD65B60_H

#define BIT(X) (1 << (X))

#define BD65B60_ENABLE_NONE 0
#define BD65B60_ENABLE_LED1 BIT(0)
#define BD65B60_ENABLE_LED2 BIT(2)
#define BD65B60_ENABLE_BOTH (BD65B60_ENABLE_LED1 | BD65B60_ENABLE_LED2)

#define BD65B60_OVP_25V 0
#define BD65B60_OVP_30V BIT(4)
#define BD65B60_OVP_35V BIT(5)

#endif /* _DT_BINDINGS_LEDS_BD65B60_H */
