// Copyright (c) Benjamin Kietzman (github.com/bkietz)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef DBUS_MESSAGE_HPP
#define DBUS_MESSAGE_HPP

#include <dbus/dbus.h>
#include <dbus/element.hpp>
#include <dbus/endpoint.hpp>
#include <dbus/impl/message_iterator.hpp>
#include <dbus/support.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

namespace dbus {

class message {
 private:
  std::shared_ptr<DBusMessage> message_;

 public:
  /// Create a method call message
  static message new_call(const endpoint& destination) {
    return dbus_message_new_method_call(
        destination.get_process_name().c_str(), destination.get_path().c_str(),
        destination.get_interface().c_str(), destination.get_member().c_str());
  }

  /// Create a method call message
  static message new_call(const endpoint& destination,
                          const string& method_name) {
    return dbus_message_new_method_call(
        destination.get_process_name().c_str(), destination.get_path().c_str(),
        destination.get_interface().c_str(), method_name.c_str());
  }

  /// Create a method return message
  static message new_return(message& call) {
    return dbus_message_new_method_return(call);
  }

  /// Create an error message
  static message new_error(message& call, const string& error_name,
                           const string& error_message) {
    return dbus_message_new_error(call, error_name.c_str(),
        error_message.c_str());
  }

  /// Create a signal message
  static message new_signal(const endpoint& origin, const string& signal_name) {
    return dbus_message_new_signal(origin.get_path().c_str(),
        origin.get_interface().c_str(),
        signal_name.c_str());
  }

  message() : message_(nullptr) {}

  message(std::nullptr_t) : message_(nullptr) {}
  message(DBusMessage* m)
    : message_{
        dbus_message_ref(m),
        [] (DBusMessage* p) { if (p) dbus_message_unref(p); }
      }
  {}

  operator DBusMessage*() { return message_.get(); }

  operator const DBusMessage*() const { return message_.get(); }

  string get_path() const {
    return sanitize(dbus_message_get_path(message_.get()));
  }

  string get_interface() const {
    return sanitize(dbus_message_get_interface(message_.get()));
  }

  string get_member() const {
    return sanitize(dbus_message_get_member(message_.get()));
  }

  string get_type() const {
    return sanitize(
        dbus_message_type_to_string(dbus_message_get_type(message_.get())));
  }

  string get_signature() const {
    return sanitize(dbus_message_get_signature(message_.get()));
  }

  string get_sender() const {
    return sanitize(dbus_message_get_sender(message_.get()));
  }

  string get_destination() const {
    return sanitize(dbus_message_get_destination(message_.get()));
  }

  uint32 get_serial() const { return dbus_message_get_serial(message_.get()); }

  message& set_serial(uint32 serial) {
    dbus_message_set_serial(message_.get(), serial);
    return *this;
  }

  uint32 get_reply_serial() const {
    return dbus_message_get_reply_serial(message_.get());
  }

  message& set_reply_serial(uint32 reply_serial) {
    dbus_message_set_reply_serial(message_.get(), reply_serial);
    return *this;
  }

  std::size_t get_args_num() {
    impl::message_iterator iter_;
    impl::message_iterator::init(*this, iter_);
    std::size_t num = 0;
    while (iter_.get_arg_type() != DBUS_TYPE_INVALID) {
      iter_.next();
      ++num;
    }
    return num;
  }

  bool set_destination(const string& destination) {
    return dbus_message_set_destination(message_.get(), destination.c_str());
  }

  struct packer {
    impl::message_iterator iter_;
    packer(message& m) { impl::message_iterator::init_append(m, iter_); }
    packer(){};

    template <typename Element, typename... Args>
    bool pack(const Element& e, const Args&... args) {
      if (this->pack(e) == false) {
        return false;
      } else {
        return pack(args...);
      }
    }

    template <typename Element>
    std::enable_if_t<is_fixed_type<Element>::value, bool> pack(
        const Element& e) {
      return iter_.append_basic(element<Element>::code, &e);
    }

    template <typename Element>
    std::enable_if_t<std::is_pointer<Element>::value, bool> pack(
        const Element e) {
      return pack(*e);
    }

    template <typename Container>
    std::enable_if_t<has_const_iterator<Container>::value &&
                     !is_string_type<Container>::value,
                     bool>
    pack(const Container& c) {
      message::packer sub;

      static const constexpr auto signature =
          element_signature<Container>::code;
      if (iter_.open_container(signature[0], &signature[1], sub.iter_) ==
          false) {
        return false;
      }
      for (auto& element : c) {
        if (!sub.pack(element)) {
          return false;
        }
      }
      return iter_.close_container(sub.iter_);
    }

    bool pack(const char* c) {
      return iter_.append_basic(element<string>::code, &c);
    }

    // bool pack specialization
    bool pack(bool c) {
      int v = c;
      return iter_.append_basic(element<bool>::code, &v);
    }

    template <typename Key, typename Value>
    bool pack(const std::pair<Key, Value> element) {
      message::packer dict_entry;
      if (iter_.open_container(DBUS_TYPE_DICT_ENTRY, NULL, dict_entry.iter_) ==
          false) {
        return false;
      }
      if (dict_entry.pack(element.first) == false) {
        return false;
      };
      if (dict_entry.pack(element.second) == false) {
        return false;
      };
      return iter_.close_container(dict_entry.iter_);
    }

    bool pack(const object_path& e) {
      const char* c = e.value.c_str();
      return iter_.append_basic(element<object_path>::code, &c);
    }

    bool pack(const string& e) {
      const char* c = e.c_str();
      return pack(c);
    }

    bool pack(const dbus_variant& v) {
      // Get the dbus typecode  of the variant being packed
      const char* type = std::visit(
          [&](auto val) {
            static const constexpr auto sig =
                element_signature<decltype(val)>::code;
            return &sig[0];
          },
          v);
      message::packer sub;
      iter_.open_container(element<dbus_variant>::code, type, sub.iter_);
      std::visit([&](const auto& val) { sub.pack(val); }, v);
      iter_.close_container(sub.iter_);

      return true;
    }
  };

  template <typename... Args>
  bool pack(const Args&... args) {
    return packer(*this).pack(args...);
  }

  // noop for template functions that have no arguments
  bool pack() { return true; }

  struct unpacker {
    impl::message_iterator iter_;
    unpacker(message& m) { impl::message_iterator::init(m, iter_); }
    unpacker() {}

    template <typename Element, typename... Args>
    bool unpack(Element& e, Args&... args) {
      if (unpack(e) == false) {
        return false;
      }
      return unpack(args...);
    }

    // Basic type unpack
    template <typename Element>
    std::enable_if_t<is_fixed_type<Element>::value, bool> unpack(
        Element& e) {
      if (iter_.get_arg_type() != element<Element>::code) {
        return false;
      }
      iter_.get_basic(&e);
      // ignoring return code here, as we might hit last element, and don't
      // really care because get_arg_type will return invalid if we call it
      // after we're over the struct boundary
      iter_.next();
      return true;
    }

    // bool unpack specialization
    bool unpack(bool& s) {
      if (iter_.get_arg_type() != element<bool>::code) {
        return false;
      }
      int c;
      iter_.get_basic(&c);
      s = c;
      iter_.next();
      return true;
    }

    // std::string unpack specialization
    bool unpack(string& s) {
      if (iter_.get_arg_type() != element<string>::code) {
        return false;
      }
      const char* c;
      iter_.get_basic(&c);
      s.assign(c);
      iter_.next();
      return true;
    }

    // object_path unpack specialization
    bool unpack(object_path& s) {
      if (iter_.get_arg_type() != element<object_path>::code) {
        return false;
      }
      const char* c;
      iter_.get_basic(&c);
      s.value.assign(c);
      iter_.next();
      return true;
    }

    // object_path unpack specialization
    bool unpack(signature& s) {
      if (iter_.get_arg_type() != element<signature>::code) {
        return false;
      }
      const char* c;
      iter_.get_basic(&c);
      s.value.assign(c);
      iter_.next();
      return true;
    }

    // variant unpack specialization
    bool unpack(dbus_variant& v) {
      if (iter_.get_arg_type() != element<dbus_variant>::code) {
        return false;
      }
      message::unpacker sub;
      iter_.recurse(sub.iter_);

      char arg_type = sub.iter_.get_arg_type();

      variant::for_each<dbus_variant>([&](auto t) {
        if (arg_type == element<decltype(t)>::code) {
          decltype(t) val_to_fill{};
          sub.unpack(val_to_fill);
          v = val_to_fill;
        }
      });

      iter_.next();
      return true;
    }

    // dict entry unpack specialization
    template <typename Key, typename Value>
    bool unpack(std::pair<Key, Value>& v) {
      auto this_code = iter_.get_arg_type();
      // This can't use element<std::pair> because there is a difference between
      // the dbus type code 'e' and the dbus signature code for dict entries
      // '{'.  Element_signature will return the full signature, and will never
      // return e, but we still want to type check it before recursing
      if (this_code != DBUS_TYPE_DICT_ENTRY) {
        return false;
      }
      message::unpacker sub;
      iter_.recurse(sub.iter_);
      if (!sub.unpack(v.first)) {
        return false;
      }
      if (!sub.unpack(v.second)) {
        return false;
      }

      iter_.next();
      return true;
    }

    template <typename T>
    struct has_emplace_method

    {
      struct dummy {};

      template <typename C, typename P>
      static auto test(P* p)
          -> decltype(std::declval<C>().emplace(*p), std::true_type());

      template <typename, typename>
      static std::false_type test(...);

      typedef decltype(test<T, dummy>(nullptr)) type;

      static constexpr bool value =
          std::is_same<std::true_type,
                       decltype(test<T, dummy>(nullptr))>::value;
    };

    template <typename T>
    struct has_emplace_back_method

    {
      struct dummy {};

      template <typename C, typename P>
      static auto test(P* p)
          -> decltype(std::declval<C>().emplace_back(*p), std::true_type());

      template <typename, typename>
      static std::false_type test(...);

      typedef decltype(test<T, dummy>(nullptr)) type;

      static constexpr bool value =
          std::is_same<std::true_type,
                       decltype(test<T, dummy>(nullptr))>::value;
    };

    template <typename Container>
    std::enable_if_t<has_emplace_back_method<Container>::value &&
                     !is_string_type<Container>::value,
                     bool>
    unpack(Container& c) {
      auto top_level_arg_type = iter_.get_arg_type();
      constexpr auto type = element_signature<Container>::code[0];
      if (top_level_arg_type != type) {
        return false;
      }
      message::unpacker sub;

      iter_.recurse(sub.iter_);
      while (sub.iter_.get_arg_type() != DBUS_TYPE_INVALID) {
        c.emplace_back();
        if (!sub.unpack(c.back())) {
          return false;
        }
      }
      iter_.next();
      return true;
    }

    template <typename Container>
    std::enable_if_t<has_emplace_method<Container>::value &&
                     !is_string_type<Container>::value,
                     bool>
    unpack(Container& c) {
      auto top_level_arg_type = iter_.get_arg_type();
      constexpr auto type = element_signature<Container>::code[0];
      if (top_level_arg_type != type) {
        return false;
      }
      message::unpacker sub;

      iter_.recurse(sub.iter_);
      while (sub.iter_.get_arg_type() != DBUS_TYPE_INVALID) {
        // TODO(ed) this is done as an unpack to a stack variable, then an
        // emplace move into the container (as the key isn't known until the
        // unpack is done)  This could be made more efficient by unpacking only
        // the key type into a temporary, using find on the temporary, then
        // unpacking directly into the map type, instead of unpacking both key
        // and value.

        typename Container::value_type t;
        if (!sub.unpack(t)) {
          return false;
        }
        c.emplace(std::move(t));
      }
      iter_.next();
      return true;
    }
  };

  template <typename... Args>
  bool unpack(Args&... args) {
    return unpacker(*this).unpack(args...);
  }

 private:
  static std::string sanitize(const char* str) {
    return (str == NULL) ? "(null)" : str;
  }
};

inline std::ostream& operator<<(std::ostream& os, const message& m) {
  os << "type='" << m.get_type() << "',"
     << "sender='" << m.get_sender() << "',"
     << "interface='" << m.get_interface() << "',"
     << "member='" << m.get_member() << "',"
     << "path='" << m.get_path() << "',"
     << "destination='" << m.get_destination() << "'";
  return os;
}

}  // namespace dbus

// primary template.
template <class T>
struct function_traits : function_traits<decltype(&T::operator())> {};

// partial specialization for function type
template <class R, class... Args>
struct function_traits<R(Args...)> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
  using decayed_arg_types = std::tuple<std::decay_t<Args>...>;

  static constexpr size_t nargs = sizeof...(Args);

  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, argument_types>::type;
  };
};

// partial specialization for function pointer
template <class R, class... Args>
struct function_traits<R (*)(Args...)> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
  using decayed_arg_types = std::tuple<std::decay_t<Args>...>;

  static constexpr size_t nargs = sizeof...(Args);

  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, argument_types>::type;
  };
};

// partial specialization for std::function
template <class R, class... Args>
struct function_traits<std::function<R(Args...)>> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
  using decayed_arg_types = std::tuple<std::decay_t<Args>...>;

  static constexpr size_t nargs = sizeof...(Args);

  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, argument_types>::type;
  };
};

// partial specialization for pointer-to-member-function (i.e., operator()'s)
template <class T, class R, class... Args>
struct function_traits<R (T::*)(Args...)> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
  using decayed_arg_types = std::tuple<std::decay_t<Args>...>;

  static constexpr size_t nargs = sizeof...(Args);

  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, argument_types>::type;
  };
};

template <class T, class R, class... Args>
struct function_traits<R (T::*)(Args...) const> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
  using decayed_arg_types = std::tuple<std::decay_t<Args>...>;

  static constexpr size_t nargs = sizeof...(Args);

  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, argument_types>::type;
  };
};

template <class F, size_t... Is>
constexpr auto index_apply_impl(F f, std::index_sequence<Is...>) {
  return f(std::integral_constant<size_t, Is>{}...);
}

template <size_t N, class F>
constexpr auto index_apply(F f) {
  return index_apply_impl(f, std::make_index_sequence<N>{});
}

template <class Tuple, class F>
constexpr auto apply(F f, Tuple& t) {
  return index_apply<std::tuple_size<Tuple>{}>(
      [&](auto... Is) { return f(std::get<Is>(t)...); });
}

template <class Tuple>
constexpr bool unpack_into_tuple(Tuple& t, dbus::message& m) {
  return index_apply<std::tuple_size<Tuple>{}>(
      [&](auto... Is) { return m.unpack(std::get<Is>(t)...); });
}

// Specialization for empty tuples.  No need to unpack if no arguments
constexpr bool unpack_into_tuple(std::tuple<>& t, dbus::message& m) {
  return true;
}

template <class Tuple>
inline bool validate_args_num(dbus::message& m) {
  return m.get_args_num() == std::tuple_size<Tuple>{};
}

template <typename... Args>
constexpr bool pack_tuple_into_msg(std::tuple<Args...>& t, dbus::message& m) {
  return index_apply<std::tuple_size<std::tuple<Args...>>{}>(
      [&](auto... Is) { return m.pack(std::get<Is>(t)...); });
}

// Specialization for empty tuples.  No need to pack if no arguments
constexpr bool pack_tuple_into_msg(std::tuple<>& t, dbus::message& m) {
  return true;
}

// Specialization for single types.  Used when callbacks simply return one value
template <typename Element>
constexpr bool pack_tuple_into_msg(Element& t, dbus::message& m) {
  return m.pack(t);
}

#include <dbus/impl/message_iterator.ipp>

#endif  // DBUS_MESSAGE_HPP
