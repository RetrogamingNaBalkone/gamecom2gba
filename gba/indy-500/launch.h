#ifndef INDY_LAUNCH_H
#define INDY_LAUNCH_H
/* native_handlers enables the PCM fast path and idle-loop hints. Without them the
 * timing follows the Tigerbyte reference cycle for cycle. */
void indy_launch(int native_handlers);
#endif
