/* Level sensors with the 2 s debounce of design.md §5.3. */

#ifndef PUMP_LEVEL_H_
#define PUMP_LEVEL_H_

#include <stdbool.h>
#include <stdint.h>

int level_init(void);
void level_tick(int64_t now_ms);

bool level_low_wet(void);  /* debounced */
bool level_high_wet(void);
bool level_low_raw(void);
bool level_high_raw(void);

#endif /* PUMP_LEVEL_H_ */
