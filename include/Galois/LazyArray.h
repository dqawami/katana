/** Lazy Static Array -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2012, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @description
 * 
 * Implements something close to std::array, but which does not initialize its
 * elements.  It is the user's responsibility to make sure memory is properly
 * initialized before using.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#ifndef GALOIS_LAZYARRAY_H
#define GALOIS_LAZYARRAY_H

#include <iterator>
#include <stdexcept>

namespace Galois {

/**
 * This is a container that encapsulates space for a constant size array.  The
 * initialization and destruction of items is explicitly under the control of
 * the user.
 */
template<typename _Tp, unsigned _Size>
class LazyArray {
  char __datac[sizeof(_Tp[(_Size > 0 ? _Size : 1)])];

  _Tp* get(size_t __n) {
    return &(reinterpret_cast<_Tp*>(&__datac[0]))[__n];
  }
  const _Tp* get(size_t __n) const {
    return &(reinterpret_cast<_Tp*>(&__datac[0]))[__n];
  }

public:
  typedef _Tp value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef value_type& reference;
  typedef const value_type& const_reference;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef pointer iterator;
  typedef const_pointer const_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  //iterators:
  iterator begin() { return iterator(get(0)); }
  const_iterator begin() const { return const_iterator(get(0)); }
  iterator end() { return iterator(get(_Size)); }
  const_iterator end() const { return const_iterator(get(_Size)); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  //capacity:
  size_type size() const { return _Size; }
  size_type max_size() const { return _Size; }
  bool empty() const { return _Size == 0; }

  //element access:
  reference operator[](size_type __n) { return *get(__n); }
  const_reference operator[](size_type __n) const { return *get(__n); }
  reference at(size_type __n) {
    if (__n >= _Size)
      throw std::out_of_range("lazyArray::at");
    return get(__n);
  }
  const_reference at(size_type __n) const {
    if (__n >= _Size)
      throw std::out_of_range("lazyArray::at");
    return get(__n);
  }  

  reference front() { return *get(0); }
  const_reference front() const { return *get(0); }
  reference back() { return *get(_Size > 0 ? _Size - 1 : 0); }
  const_reference back() const { return *get(_Size > 0 ? _Size - 1 : 0); }

  pointer data() { return &get(0); }
  const_pointer data() const { return &get(0); }

  //missing: fill swap

  //Extra functionality
#ifdef GALOIS_HAS_RVALUE_REFERENCES
  template<typename... Args>
  pointer emplace(size_type __n, Args&&... val) { return new (get(__n)) _Tp(std::forward<Args>(val)...); }

  pointer construct(size_type __n, const _Tp& val) { return emplace(__n, val); }
  pointer construct(size_type __n, _Tp&& val) { return emplace(__n, std::move(val)); }
#else
  pointer construct(size_type __n, const _Tp& val) { return new (get(__n)) _Tp(val); }
#endif

  void destroy(size_type __n) { (get(__n))->~_Tp(); }
};

}
#endif // GALOIS_LAZYARRAY_H
