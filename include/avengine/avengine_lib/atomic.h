
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

  int get_val() const
  {
    return __atomic_load_n(&val, __ATOMIC_SEQ_CST);
  }
};

} // namespace avengine