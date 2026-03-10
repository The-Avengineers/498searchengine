#include "string.h"

namespace avengine
{

struct error
{
  string msg;

  error() {}
  error(const char* m) : msg(m) {}
  error(const String& m) : msg(m) {}
};

// for returning non void

// example usage:

// Result<char, Error> res = Result<void, Error>::ok('C');
// if (!res.hasValue) {
//      handle(r.error());
// }

template <typename T, typename E = error> class expected
{
public:
  static expected ok(const T& val)
  {
    expected r;
    r.val_ = val;
    r.hasVal_ = true;
    return r;
  }

  static expected error(const E& err)
  {
    expected r;
    r.err_ = err;
    r.hasVal_ = false;
    return r;
  }

  bool hasValue() const
  {
    return hasVal_;
  }

  T& value()
  {
    return val_;
  }
  const T& value() const
  {
    return val_;
  }

  E& error()
  {
    return err_;
  }
  const E& error() const
  {
    return err_;
  }

private:
  expected() : hasVal_(false) {}

  T val_;
  E err_;
  bool hasVal_;
};

// for returning void since we cant declare a variable of type void

// usage:

// Result<void, Error> res = Result<void, Error>::ok();
// if (!res.hasValue) {
//      handle(r.error());
// }

template <typename E> class expected<void, E>
{
public:
  static expected ok()
  {
    expected r;
    r.hasVal_ = true;
    return r;
  }

  static expected error(const E& err)
  {
    expected r;
    r.err_ = err;
    r.hasVal_ = false;
    return r;
  }

  bool hasValue() const
  {
    return hasVal_;
  }

  E& error()
  {
    return err_;
  }
  const E& error() const
  {
    return err_;
  }

private:
  expected() : hasVal_(false) {}
  E err_;
  bool hasVal_;
};

} // namespace avengine