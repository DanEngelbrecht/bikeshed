|Branch      | OSX / Linux / Windows |
|------------|-----------------------|
|master      | [![Build Status](https://travis-ci.org/DanEngelbrecht/bikeshed.svg?branch=master)](https://travis-ci.org/DanEngelbrecht/bikeshed?branch=master) |
|master      | [![Codacy Badge](https://api.codacy.com/project/badge/Grade/3f4c844382cc4314ada8d8c7ac27e544)](https://app.codacy.com/app/DanEngelbrecht/bikeshed?utm_source=github.com&utm_medium=referral&utm_content=DanEngelbrecht/bikeshed&utm_campaign=Badge_Grade_Dashboard) |

# bikeshed
Lock free hierarchical work scheduler
Builds with MSVC, Clang and GCC, header only, C99 compliant, MIT license.

See github for latest version: https://github.com/DanEngelbrecht/bikeshed

See design blogs at: https://danengelbrecht.github.io

## Version history

### Version v0.4 18/5 2019

**Pre-release 4**

### API changes
  - Bikeshed_AddDependencies to take array of parent task task_ids
  - Bikeshed_ReadyCallback now gets called per channel range

### API additions
  - Bikeshed_FreeTasks
  - BIKESHED_L1CACHE_SIZE to control ready head alignement - no padding/alignement added by default
  - BIKESHED_CPU_YIELD to control yielding in high-contention scenarios

### Fixes
  - Added default (non-atomic) implementations for helper for unsupported platforms/compilers
  - Codacy analisys and badge
  - More input validation on public apis when BIKESHED_ASSERTS is enabled
  - Batch allocation of task indexes
  - Batch allocation of dependency indexes
  - Batch free of task indexes
  - Batch free of dependency indexes
  - Fixed broken channel range detection when resolving dependencies

### Version v0.3 1/5 2019

**Pre-release 3**

#### Fixes

- Ready callback is now called when a task is readied via dependency resolve
- Tasks are readied in batch when possible

### Version v0.2 29/4 2019

**Pre-release 2**

#### Fixes

- Internal cleanups
- Fixed warnings and removed clang warning suppressions
  - `-Wno-sign-conversion`
  - `-Wno-unused-macros`
  - `-Wno-c++98-compat`
  - `-Wno-implicit-fallthrough`
- Made it compile cleanly with clang++ on Windows

### Version v0.1 26/4 2019

**Pre-release 1**

## Features
- Generic tasks scheduling with dependecies between tasks
  - Each task has zero or many dependecies (as defined by user)
  - User should Ready any tasks that can execute (has zero dependencies)
  - Automatic ready of tasks that reaches zero dependecies
  - Automatic free of tasks that has completed
- A task can have many parents and many child dependecies
- Task channels - execute tasks based on task channel
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
- Tagging of tasks

## Dependencies
Minimal dependecies with default overridable method for atomic operations.
 - `<stdint.h>`
 - `<string.h>`
 - The default (optional) MSVC implementation depends on `<Windows.h>`.

### Optional default methods
The default implementations for the atomic functions can be overridden with your own implementation by overriding the macros:
 - `BIKESHED_ATOMICADD` Atomically adds a 32-bit signed integer to another 32-bit signed integer and returns the result
 - `BIKESHED_ATOMICCAS` Atomically exchange a 32-bit signed integer with another 32-bit signed integer if the value to be swapped matches the provided compare value, returns the old value.

## Test code dependecies

Test code has dependencies added as drop-in headers from
 - https://github.com/JCash/jctest for unit test validation

Test code has dependencies added as git sub-modules from
 - https://github.com/DanEngelbrecht/nadir for threading and syncronization
