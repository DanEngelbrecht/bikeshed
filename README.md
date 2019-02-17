|Branch      | OSX / Linux / Windows |
|------------|-----------------------|
|master      | [![Build Status](https://travis-ci.org/DanEngelbrecht/bikeshed.svg?branch=master)](https://travis-ci.org/DanEngelbrecht/bikeshed?branch=master) |

# bikeshed
Super simple work scheduler

## Features
- Generic tasks scheduling with dependecies between tasks
  - Each task has zero or many dependecies (as defined by user)
  - User should Ready any tasks that can execute (has zero dependencies)
  - Automatic ready of tasks that reaches zero dependecies
  - Automatic free of tasks that has completed
- A task can have many parents and many child dependecies
- No memory allocations once shed is created
- Tasks can yield and block
- Only depends on one header - <stdint.h>
- Memory allocation and threading are users responsability
  - Easy API for threading and syncronization provided
- Lifetime of data associated with tasks is users responsability
- Configurable and optional assert (fatal error) behavior

## Non-features
- Cyclic dependency detection and resolving
  - API is designed to help user avoid cyclic dependecies but does not do any analisys
- Built in threading or syncronization code - API to add it is available
- Unlimited number of active tasks - currently limited to 65535 *active* tasks
- Cancelling of tasks
- Grouping of tasks
- Tagging of tasks

## Dependencies
- Code dependencies
    - <stdint.h>
- Test code has dependencies added as git sub-modules
    - https://github.com/DanEngelbrecht/jctest for unit test validation
    - https://github.com/DanEngelbrecht/nadir for threading and syncronization
