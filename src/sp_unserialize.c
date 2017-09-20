#include "php_snuffleupagus.h"

ZEND_DECLARE_MODULE_GLOBALS(snuffleupagus)

PHP_FUNCTION(sp_serialize) {
  void (*orig_handler)(INTERNAL_FUNCTION_PARAMETERS);

  /* Call the original `serialize` function. */
  if ((orig_handler = zend_hash_str_find_ptr(SNUFFLEUPAGUS_G(sp_internal_functions_hook),
                                             "serialize", 9))) {
    orig_handler(INTERNAL_FUNCTION_PARAM_PASSTHRU);
  } else {
    sp_log_err("disabled_functions",
        "Unable to find the pointer to the original function 'serialize' in "
        "the hashtable.\n");
  }

  /* Compute the HMAC of the textual representation of the serialized data*/
  zval func_name;
  zval hmac;
  zval params[3];

  ZVAL_STRING(&func_name, "hash_hmac");
  ZVAL_STRING(&params[0], "sha256");
  params[1] = *return_value;
  ZVAL_STRING(&params[2],
              SNUFFLEUPAGUS_G(config).config_snuffleupagus->encryption_key);
  call_user_function(CG(function_table), NULL, &func_name, &hmac, 3, params);

  size_t len = Z_STRLEN_P(return_value) + Z_STRLEN(hmac);
  zend_string *res = zend_string_alloc(len, 0);

  memcpy(ZSTR_VAL(res), Z_STRVAL_P(return_value), Z_STRLEN_P(return_value));
  memcpy(ZSTR_VAL(res) + Z_STRLEN_P(return_value), Z_STRVAL(hmac),
         Z_STRLEN(hmac));
  ZSTR_VAL(res)[len] = '\0';

  /* Append the computed HMAC to the serialized data. */
  return_value->value.str = res;
  return;
}

PHP_FUNCTION(sp_unserialize) {
  void (*orig_handler)(INTERNAL_FUNCTION_PARAMETERS);

  char *buf = NULL;
  char *serialized_str = NULL;
  char *hmac = NULL;
  zval expected_hmac;
  size_t buf_len = 0;
  zval *opts = NULL;

  if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|a", &buf, &buf_len, &opts) ==
      FAILURE) {
    RETURN_FALSE;
  }

  /* 64 is the length of HMAC-256 */
  if (buf_len < 64) {
    sp_log_msg("unserialize", SP_LOG_DROP, "The serialized object is too small.");
    RETURN_FALSE;
  }

  hmac = buf + buf_len - 64;
  serialized_str = ecalloc(sizeof(*serialized_str) * (buf_len - 64 + 1), 1);
  memcpy(serialized_str, buf, buf_len - 64);

  zval func_name;
  ZVAL_STRING(&func_name, "hash_hmac");

  zval params[3];
  ZVAL_STRING(&params[0], "sha256");
  ZVAL_STRING(&params[1], serialized_str);
  ZVAL_STRING(&params[2],
              SNUFFLEUPAGUS_G(config).config_snuffleupagus->encryption_key);
  call_user_function(CG(function_table), NULL, &func_name, &expected_hmac, 3,
                     params);

  unsigned int status = 0;
  for (uint8_t i = 0; i < 64; i++) {
    status |= (hmac[i] ^ (Z_STRVAL(expected_hmac))[i]);
  }

  if (0 == status) {
    if ((orig_handler = zend_hash_str_find_ptr(SNUFFLEUPAGUS_G(sp_internal_functions_hook),
                                               "unserialize", 11))) {
      orig_handler(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
  } else {
    if ( true == SNUFFLEUPAGUS_G(config).config_unserialize->simulation) {
      sp_log_msg("unserialize", SP_LOG_NOTICE, "Invalid HMAC for %s", serialized_str);
      if ((orig_handler = zend_hash_str_find_ptr(SNUFFLEUPAGUS_G(sp_internal_functions_hook),
                                               "unserialize", 11))) {
        orig_handler(INTERNAL_FUNCTION_PARAM_PASSTHRU);
      }
    } else {
      sp_log_msg("unserialize", SP_LOG_DROP, "Invalid HMAC for %s", serialized_str);
    }
  }
  efree(serialized_str);
  return;
}

int hook_serialize(void) {
  TSRMLS_FETCH();

  HOOK_FUNCTION("serialize", sp_internal_functions_hook, PHP_FN(sp_serialize), false);
  HOOK_FUNCTION("unserialize", sp_internal_functions_hook, PHP_FN(sp_unserialize), false);

  return SUCCESS;
}
