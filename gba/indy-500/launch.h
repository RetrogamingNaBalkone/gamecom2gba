#ifndef INDY_LAUNCH_H
#define INDY_LAUNCH_H
/* native_handlers: bit 0 enables the PCM fast path, bit 1 the idle-loop hints. Without them the
 * timing follows the Tigerbyte reference cycle for cycle. */
void indy_launch(int native_handlers);
#endif
