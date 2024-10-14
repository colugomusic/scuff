#pragma once

#include <algorithm>
#include <boost/config.hpp>
#include <boost/preprocessor.hpp>
#include <variant>
#include <type_traits>

template<class T> struct remove_cvref { typedef std::remove_cv_t<std::remove_reference_t<T>> type; };
template< class T > using remove_cvref_t = typename remove_cvref<T>::type;

// I found this on reddit.
// https://www.reddit.com/r/cpp/comments/kst2pu/comment/giilcxv/
template<typename F, typename V, size_t I = 0>
BOOST_FORCEINLINE decltype(auto) fast_visit(F&& f, V&& v) {
	if constexpr (I == 0) {
		if (BOOST_UNLIKELY(v.valueless_by_exception())) {
			throw std::bad_variant_access{};
		}
	} 
	constexpr auto vs = std::variant_size_v<remove_cvref_t<decltype(v)>>; 
#define _VISIT_CASE_CNT 32
#define _VISIT_CASE(Z, N, D)                                                                                      \
		case I + N: {                                                                                             \
			if constexpr(I + N < vs) {                                                                            \
				return std::forward<decltype(f)>(f)(std::get<I + N>(std::forward<decltype(v)>(v)));               \
			}                                                                                                     \
			else {                                                                                                \
				BOOST_UNREACHABLE_RETURN(std::forward<decltype(f)>(f)(std::get<0>(std::forward<decltype(v)>(v))));\
			}                                                                                                     \
		}                                                                                                         \
		/**/
	switch (v.index()) {
		BOOST_PP_REPEAT(
			_VISIT_CASE_CNT,
			_VISIT_CASE, _)
	}
	constexpr auto next_idx = std::min(I + _VISIT_CASE_CNT, vs); 
	// if constexpr(next_idx < vs) causes some weird msvc bug
	if constexpr (next_idx + 0 < vs) {
		return fast_visit<F, V, next_idx>(std::forward<decltype(f)>(f), std::forward<decltype(v)>(v));
	}
	BOOST_UNREACHABLE_RETURN(std::forward<decltype(f)>(f)(std::get<0>(std::forward<decltype(v)>(v))));
#undef _VISIT_CASE_CNT
#undef _VISIT_CASE
}
