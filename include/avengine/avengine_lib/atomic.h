
namespace avengine
{

struct atomic_counter
{
  alignas(32) int val;
  atomic_counter(int v = 0) : val(v) {}

  int fetch_add(int n)
  {
    return __atomic_fetch_add(&val, n, __ATOMIC_SEQ_CST);
  }

  void store(int n)
  {
    __atomic_store_n(&val, n, __ATOMIC_SEQ_CST);
  }

  int get_val() const
  {
    return __atomic_load_n(&val, __ATOMIC_SEQ_CST);
  }

  bool load() const
  {
    return __atomic_load_n(&val, __ATOMIC_SEQ_CST) != 0;
  }
};

} // namespace avengine