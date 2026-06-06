### 0.4
- Split `sorted_args_filter` into `sorted_args_remove_args` and `sorted_args_keep_args`
- Rename `sorted_args_clear_empty_args` to `sorted_args_clear_valueless_args`
- Add wildcard argument filters, configurable sort order, and duplicate handling
- Add `sorted_args_keep_args *` to disable inherited filters
- Add `sorted_args_remove_args *` to clear all arguments
- Add `sorted_args_clear_invalid_args` to remove arguments with empty keys
- Add `''` as a regular filter name to match empty-key parameters

### 0.3
- Add support to the module works as a dynamic module

### 0.2
- [bug fix] Correct the variable length when all parameters were filtered

### 0.1
- Initial release
