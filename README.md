|Branch      | OSX / Linux / Windows |
|------------|-----------------------|
|master      | [![Build Status](https://travis-ci.org/DanEngelbrecht/bikeshed.svg?branch=master)](https://travis-ci.org/DanEngelbrecht/bikeshed?branch=master) |

# bikeshed
Lock free hierarchical work scheduler
Builds with MSVC, Clang and GCC, header only, C99 compliant, MIT license.

See github for latest version: https://github.com/DanEngelbrecht/bikeshed

See design blogs at: https://danengelbrecht.github.io

## Version history

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
