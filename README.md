|Branch      | OSX / Linux / Windows |
|------------|-----------------------|
|master      | [![Build Status](https://travis-ci.org/DanEngelbrecht/bikeshed.svg?branch=master)](https://travis-ci.org/DanEngelbrecht/bikeshed?branch=master) |

# bikeshed
Super simple work scheduler
Builds with MSVC, Clang and GCC, header only, C99 compliant, MIT license.

## Features
- Generic tasks scheduling with dependecies between tasks
  - Each task has zero or many dependecies (as defined by user)
  - User should Ready any tasks that can execute (has zero dependencies)
  - Automatic ready of tasks that reaches zero dependecies
  - Automatic free of tasks that has completed
- A task can have many parents and many child dependecies
- No memory allocations once shed is created
- Minimal dependencies
- Memory allocation and threading are users responsability
- Lifetime of data associated with tasks is users responsability
- Configurable and optional assert (fatal error) behavior
- Configurable platform dependant functions with default implementation provided
- Header only - define `BIKESHED_IMPLEMENTATION` in one compilation unit and include `bikeshed.h`

## Non-features
- Cyclic dependency detection and resolving
  - API is designed to help user avoid cyclic dependecies but does not do any analisys
- Built in threading or syncronization code - API to add it is available
- Unlimited number of active tasks - currently limited to 8 388 607 *active* tasks
- Cancelling of tasks
- Grouping of tasks
- Tagging of tasks

## Dependencies
Minimal dependecies with default overridable method for atomic operations.
 - `<stdint.h>`
 - `<stddef.h>`
 - The default (optional) MSVC implementation depends on `<Windows.h>`.

### Optional default methods
The default implementations for the atomic functions can be overridden with your own implementation by overriding the macros:
 - `BIKESHED_ATOMICADD` Atomically adds a 32-bit signed integer to another 32-bit signed integer and returns the result
 - `BIKESHED_ATOMICCAS` Atomically exchange a 32-bit signed integer with another 32-bit signed integer if the value to be swapped matches the provided compare value, returns the old value.

## Test code dependecies

Test code has dependencies added as git sub-modules
 - https://github.com/DanEngelbrecht/jctest for unit test validation
 - https://github.com/DanEngelbrecht/nadir for threading and syncronization
