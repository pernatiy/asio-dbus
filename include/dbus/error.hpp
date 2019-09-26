// Copyright (c) Benjamin Kietzman (github.com/bkietz)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef DBUS_ERROR_HPP
#define DBUS_ERROR_HPP

#include <dbus/dbus.h>
#include <dbus/element.hpp>
#include <dbus/message.hpp>
#include <asio/error_code.hpp>
#include <asio/system_error.hpp>

namespace dbus {

namespace detail {

class error_category : public asio::error_category {
  const char *name() const ASIO_NOEXCEPT { return "dbus.error"; }

  string message(int value) const {
    if (value)
      return "DBus error";
    else
      return "no error";
  }
};

}  // namespace detail

inline const asio::error_category &get_dbus_category() {
  static detail::error_category instance;
  return instance;
}

class error {
  DBusError error_;

 public:
  error() {
    dbus_error_init(&error_);
  }

  error(DBusError *src) {
    dbus_error_init(&error_);
    dbus_move_error(src, &error_);
  }

  error(dbus::message &m) {
    dbus_error_init(&error_);
    dbus_set_error_from_message(&error_, m);
  }

  ~error() {
    if (is_set()) {
      dbus_error_free(&error_);
    }
  }

  bool is_set() const { return dbus_error_is_set(&error_); }

  operator const DBusError *() const { return &error_; }

  operator DBusError *() { return &error_; }

  asio::error_code error_code() const;
  asio::system_error system_error() const;
  void throw_if_set() const;
};

inline asio::error_code error::error_code() const {
  return asio::error_code(is_set(), get_dbus_category());
}

inline asio::system_error error::system_error() const {
  return asio::system_error(
      error_code(), string(error_.name) + ":" + error_.message);
}

inline void error::throw_if_set() const {
  if (is_set())
    asio::detail::throw_exception(system_error());
}

}  // namespace dbus

#endif  // DBUS_ERROR_HPP
