#include <string>
#include <dbus/dbus.h>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace dbus {

using std::string;

class error
  : public boost::system::error_category
{
  DBusError error_;

public:
  error()
  {
    dbus_error_init(&error_);
  }

  error(DBusError *src_)
  {
    dbus_error_init(&error_);
    dbus_move_error(src_, &error_);
  }

  ~error()
  {
    dbus_error_free(&error_);
  }

  const char *name() const BOOST_SYSTEM_NOEXCEPT
  {
    return error_.name;
  }

  string message(int value) const
  {
    return error_.message;
  }

  bool is_set() const
  {
    return dbus_error_is_set(&error_);
  }

  operator bool() const
  {
    return is_set();
  }

  operator const DBusError *() const
  {
    return &error_;
  }

  operator DBusError *()
  {
    return &error_;
  }

  boost::system::error_code error_code();
  boost::system::system_error system_error();
  void throw_if_set();
};

boost::system::error_code error::error_code()
{
  return boost::system::error_code(
      is_set(),
      *this);
}

boost::system::system_error error::system_error()
{
  return boost::system::system_error(error_code());
}

void error::throw_if_set()
{
  if(is_set()) throw system_error();
}

} // namespace dbus
