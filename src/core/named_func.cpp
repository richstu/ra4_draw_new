/*! \class NamedFunc

  \brief Combines a callable function taking a Baby and returning a scalar or
  vector with its string representation.

  NamedFunc contains both a callabale function which takes a Baby as a parameter
  and returns either a scalar or a vector result. It also contains a string
  representation of that function. Typically, this is a C++/"TTree::Draw"-like
  expression, but can be manually set to any desired string independently of the
  callable function. Given only a C++ expression, it is able to dynamically
  generate the appropriate callable and fully compiled function. The string
  parsing is done just once by FunctionParser, and the resulting callable
  function stored for fast calling without reinterpreting the string each time.

  \link NamedFunc NamedFuncs\endlink can be manipulated in much the same way as
  C++ arithmetic types. For example, given \link NamedFunc NamedFuncs\endlink x
  and y, x+y will return a NamedFunc z whose function evaluates the functions of
  x and y and returns the sum of the results. It is important to note that z's
  function does not simply return the result it obtains by evaluating x and y on
  construction. Rather, it remembers the functions from x and y, and reevaluates
  the component addends and resulting sum each time z is called. This allows
  construction of arbitrarily complicated functions by applying standard C++
  operators to simple \link NamedFunc NamedFuncs\endlink. This ability is used
  heavily by FunctionParser to build a single NamedFunc from complex
  expressions. Currently, the operators "+" (unary and binary), "-" (unary and
  binary), "*", "/", "%", "+=", "-=", "*=", "/=", "%=", "==", "!=", ">", "<",
  ">=", "<=", "&&", "||", and "!" are supported. Bit-level operators "<<", ">>",
  "~", "^", "^=", "&=", and "|=" and not supported. The "<<" is used for
  printing to an output stream.

  The current implementation keeps both a scalar and vector function internally,
  only one of which is valid at any time. To the scalar function is evaluated
  with NamedFunc::GetScalar(), while the vector function is evaluated with
  NamedFunc::GetVector(). A possible alternative is to always return a vector
  (of length 1 in the case of a scalar result), and use a bool to determine if
  the result should be considered a scalar syntactically. This would allow
  NamedFunc to act as a true functor with a cleaner interface, but results in
  extra vectors being constructed (and often copied if care is not taken with
  results) even when evaluating a simple scalar value.

  \see FunctionParser for allowed expression syntax for constructing a
  NamedFunc.
*/
#include "core/named_func.hpp"

#include <iostream>
#include <utility>

#include "core/utilities.hpp"
#include "core/function_parser.hpp"

using namespace std;

using ScalarType = NamedFunc::ScalarType;
using VectorType = NamedFunc::VectorType;
using ScalarFunc = NamedFunc::ScalarFunc;
using VectorFunc = NamedFunc::VectorFunc;

namespace{
  /*!\brief Get a functor applying unary operator op to f

    \param[in] f Function which takes a Baby and returns a single value

    \param[in] op Unary operator to apply to f

    \return Functor which takes a Baby and returns the result of applying op to
    the result of f
  */
  template<typename Operator>
    function<ScalarFunc> ApplyOp(const function<ScalarFunc> &f,
                                 const Operator &op){
    if(!static_cast<bool>(f)) return f;
    function<ScalarType(ScalarType)> op_c(op);
    return [f,op_c](const Baby &b){
      return op_c(f(b));
    };
  }

  /*!\brief Get a functor applying unary operator op to f

    \param[in] f Function which takes a Baby and returns a vector of values

    \param[in] op Unary operator to apply to f

    \return Functor which takes a Baby and returns the result of applying op to
    each element of the result of f
  */
  template<typename Operator>
    function<VectorFunc> ApplyOp(const function<VectorFunc> &f,
                                 const Operator &op){
    if(!static_cast<bool>(f)) return f;
    function<ScalarType(ScalarType)> op_c(op);
    return [f,op_c](const Baby &b){
      VectorType v = f(b);
      for(auto &x: v){
        x = op_c(x);
      }
      return v;
    };
  }

  /*!\brief Get a functor applying binary operator op to operands (sfa or vfa)
    and (sfb or vfb)

    sfa and vfa are associated to the same NamedFunc, and sfb and vfb are
    associated to the same NamedFunc. Exactly one from each pair should be a
    valid function. Determines the valid function from each pair and applies
    binary operator op between the "a" function result on the left and the "b"
    function result on the right.

    \param[in] sfa Scalar function from the same NamedFunc as vfa

    \param[in] vfa Vector function from the same NamedFunc as sfa

    \param[in] sfb Scalar function from the same NamedFunc as vfb

    \param[in] vfb Vector function from the same NamedFunc as sfb

    \param[in] op Unary operator to apply to (sfa or vfa) and (sfb or vfb)

    \return Functor which takes a Baby and returns the result of applying op to
    (sfa or vfa) and (sfb or vfb)
  */
  template<typename Operator>
    pair<function<ScalarFunc>, function<VectorFunc> > ApplyOp(const function<ScalarFunc> &sfa,
                                                              const function<VectorFunc> &vfa,
                                                              const function<ScalarFunc> &sfb,
                                                              const function<VectorFunc> &vfb,
                                                              const Operator &op){
    function<ScalarType(ScalarType,ScalarType)> op_c(op);
    function<ScalarFunc> sfo;
    function<VectorFunc> vfo;
    if(static_cast<bool>(sfa) && static_cast<bool>(sfb)){
      sfo = [sfa,sfb,op_c](const Baby &b){
        return op_c(sfa(b), sfb(b));
      };
    }else if(static_cast<bool>(sfa) && static_cast<bool>(vfb)){
      vfo = [sfa,vfb,op_c](const Baby &b){
        ScalarType sa = sfa(b);
        VectorType vb = vfb(b);
        VectorType vo(vb.size());
        for(size_t i = 0; i < vo.size(); ++i){
          vo.at(i) = op_c(sa, vb.at(i));
        }
        return vo;
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(sfb)){
      vfo = [vfa,sfb,op_c](const Baby &b){
        VectorType va = vfa(b);
        ScalarType sb = sfb(b);
        VectorType vo(va.size());
        for(size_t i = 0; i < vo.size(); ++i){
          vo.at(i) = op_c(va.at(i), sb);
        }
        return vo;
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(vfb)){
      vfo = [vfa,vfb,op_c](const Baby &b){
        VectorType va = vfa(b);
        VectorType vb = vfb(b);
        VectorType vo(va.size() > vb.size() ? vb.size() : va.size());
        for(size_t i = 0; i < vo.size(); ++i){
          vo.at(i) = op_c(va.at(i), vb.at(i));
        }
        return vo;
      };
    }
    return make_pair(sfo, vfo);
  }

  /*!\brief Get a functor applying binary "&&" to operands (sfa or vfa) and (sfb
    or vfb)

    Replaces generic template with short-circuiting "and" logic. \see ApplyOp().

    \param[in] sfa Scalar function from the same NamedFunc as vfa

    \param[in] vfa Vector function from the same NamedFunc as sfa

    \param[in] sfb Scalar function from the same NamedFunc as vfb

    \param[in] vfb Vector function from the same NamedFunc as sfb

    \return Functor which takes a Baby and returns the result of applying op to
    (sfa or vfa) and (sfb or vfb)
  */
  template<>
    pair<function<ScalarFunc>, function<VectorFunc> > ApplyOp(const function<ScalarFunc> &sfa,
                                                              const function<VectorFunc> &vfa,
                                                              const function<ScalarFunc> &sfb,
                                                              const function<VectorFunc> &vfb,
                                                              const logical_and<ScalarType> &/*op*/){
    function<ScalarFunc> sfo;
    function<VectorFunc> vfo;
    if(static_cast<bool>(sfa) && static_cast<bool>(sfb)){
      sfo = [sfa,sfb](const Baby &b){
        return sfa(b)&&sfb(b);
      };
    }else if(static_cast<bool>(sfa) && static_cast<bool>(vfb)){
      vfo = [sfa,vfb](const Baby &b){
        ScalarType sa = sfa(b);
        if(!sa){
          return VectorType(vfb(b).size(), false);
        }else{
          return vfb(b);
        }
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(sfb)){
      vfo = [vfa,sfb](const Baby &b){
        VectorType va = vfa(b);
        VectorType vo(va.size());
        bool evaluated = false;
        ScalarType sb = 0.;
        for(size_t i = 0; i < vo.size(); ++i){
          if(!evaluated && va.at(i)){
            evaluated = true;
            sb = sfb(b);
          }
          vo.at(i) = va.at(i)&&sb;
        }
        return vo;
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(vfb)){
      vfo = [vfa,vfb](const Baby &b){
        VectorType va = vfa(b);
        VectorType vb = vfb(b);
        VectorType vo(va.size() > vb.size() ? vb.size() : va.size());
        for(size_t i = 0; i < vo.size(); ++i){
          vo.at(i) = va.at(i)&&vb.at(i);
        }
        return vo;
      };
    }
    return make_pair(sfo, vfo);
  }

  /*!\brief Get a functor applying binary "||" to operands (sfa or vfa) and (sfb
    or vfb)

    Replaces generic template with short-circuiting "or" logic. \see ApplyOp().

    \param[in] sfa Scalar function from the same NamedFunc as vfa

    \param[in] vfa Vector function from the same NamedFunc as sfa

    \param[in] sfb Scalar function from the same NamedFunc as vfb

    \param[in] vfb Vector function from the same NamedFunc as sfb

    \return Functor which takes a Baby and returns the result of applying op to
    (sfa or vfa) and (sfb or vfb)
  */
  template<>
    pair<function<ScalarFunc>, function<VectorFunc> > ApplyOp(const function<ScalarFunc> &sfa,
                                                              const function<VectorFunc> &vfa,
                                                              const function<ScalarFunc> &sfb,
                                                              const function<VectorFunc> &vfb,
                                                              const logical_or<ScalarType> &/*op*/){
    function<ScalarFunc> sfo;
    function<VectorFunc> vfo;
    if(static_cast<bool>(sfa) && static_cast<bool>(sfb)){
      sfo = [sfa,sfb](const Baby &b){
        return sfa(b)||sfb(b);
      };
    }else if(static_cast<bool>(sfa) && static_cast<bool>(vfb)){
      vfo = [sfa,vfb](const Baby &b){
        ScalarType sa = sfa(b);
        if(sa){
          return VectorType(vfb(b).size(), true);
        }else{
          return vfb(b);
        }
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(sfb)){
      vfo = [vfa,sfb](const Baby &b){
        VectorType va = vfa(b);
        VectorType vo(va.size());
        bool evaluated = false;
        ScalarType sb = 0.;
        for(size_t i = 0; i < vo.size(); ++i){
          if(!(evaluated || va.at(i))){
            evaluated = true;
            sb = sfb(b);
          }
          vo.at(i) = va.at(i)||sb;
        }
        return vo;
      };
    }else if(static_cast<bool>(vfa) && static_cast<bool>(vfb)){
      vfo = [vfa,vfb](const Baby &b){
        VectorType va = vfa(b);
        VectorType vb = vfb(b);
        VectorType vo(va.size() > vb.size() ? vb.size() : va.size());
        for(size_t i = 0; i < vo.size(); ++i){
          vo.at(i) = va.at(i)||vb.at(i);
        }
        return vo;
      };
    }
    return make_pair(sfo, vfo);
  }
}

/*!\brief Constructor of a scalar NamedFunc

  \param[in] name Text representation of function

  \param[in] function Functor taking a Baby and returning a scalar
*/
NamedFunc::NamedFunc(const std::string &name,
                     const std::function<ScalarFunc> &function):
  name_(name),
  scalar_func_(function),
  vector_func_(){
  CleanName();
}

/*!\brief Constructor of a vector NamedFunc

  \param[in] name Text representation of function

  \param[in] function Functor taking a Baby and returning a vector
*/
NamedFunc::NamedFunc(const std::string &name,
                     const std::function<VectorFunc> &function):
  name_(name),
  scalar_func_(),
  vector_func_(function){
  CleanName();
  }

/*!\brief Constructor using FunctionParser to produce a real function from a
  string

  \param[in] function C++/"TTree::Draw"-like expression containing constants,
  Baby variables, operators, parenthesis, brackets, etc.
*/
NamedFunc::NamedFunc(const string &function):
  NamedFunc(FunctionParser(function).ResolveAsNamedFunc()){
}

/*!\brief Constructor using FunctionParser to produce a real function from a
  string

  \param[in] function C++/"TTree::Draw"-like expression containing constants,
  Baby variables, operators, parenthesis, brackets, etc.
*/
NamedFunc::NamedFunc(const char *function):
  NamedFunc(string(function)){
}

/*!\brief Constructor using FunctionParser to produce a real function from a
  string

  \param[in] function C++/"TTree::Draw"-like expression containing constants,
  Baby variables, operators, parenthesis, brackets, etc.
*/
NamedFunc::NamedFunc(const TString &function):
  NamedFunc(static_cast<const char *>(function)){
}

/*!\brief Constructor for NamedFunc returning a constant

  \param[in] x The constant to be returned
*/
NamedFunc::NamedFunc(ScalarType x):
  name_(ToString(x)),
  scalar_func_([x](const Baby&){return x;}),
  vector_func_(){
}

/*!\brief Get the string representation of this function

  \return The standard string representation of this function
*/
const string & NamedFunc::Name() const{
  return name_;
}

/*!\brief Set the string representation of this function

  \param[in] name String representation of the function
*/
NamedFunc & NamedFunc::Name(const string &name){
  name_ = name;
  CleanName();
  return *this;
}

/*!\brief Set function to given scalar function

  This function overwrites the scalar function and invalidates the vector
  function if set.

  \param[in] f Valid function taking a Baby and returning a scalar

  \return Reference to *this
*/
NamedFunc & NamedFunc::Function(const std::function<ScalarFunc> &f){
  if(!static_cast<bool>(f)) return *this;
  scalar_func_ = f;
  vector_func_ = function<VectorFunc>();
  return *this;
}

/*!\brief Set function to given vector function

  This function overwrites the vector function and invalidates the scalar
  function if set.

  \param[in] f Valid function taking a Baby and returning a vector

  \return Reference to *this
*/
NamedFunc & NamedFunc::Function(const std::function<VectorFunc> &f){
  if(!static_cast<bool>(f)) return *this;
  scalar_func_ = function<ScalarFunc>();
  vector_func_ = f;
  return *this;
}

/*!\brief Return the (possibly invalid) scalar function

  \return The (possibly invalid) scalar function associated to *this
*/
const function<ScalarFunc> & NamedFunc::ScalarFunction() const{
  return scalar_func_;
}

/*!\brief Return the (possibly invalid) vector function

  \return The (possibly invalid) vector function associated to *this
*/
const function<VectorFunc> & NamedFunc::VectorFunction() const{
  return vector_func_;
}

/*!\brief Check if scalar function is valid

  \return True if scalar function is valid; false otherwise.
*/
bool NamedFunc::IsScalar() const{
  return static_cast<bool>(scalar_func_);
}

/*!\brief Check if vectorr function is valid

  \return True if vector function is valid; false otherwise.
*/
bool NamedFunc::IsVector() const{
  return static_cast<bool>(vector_func_);
}

/*!\brief Evaluate scalar function with b as argument

  \param[in] b Baby to pass to scalar function

  \return Result of applying scalar function to b
*/
ScalarType NamedFunc::GetScalar(const Baby &b) const{
  return scalar_func_(b);
}

/*!\brief Evaluate vector function with b as argument

  \param[in] b Baby to pass to vector function

  \return Result of applying vector function to b
*/
VectorType NamedFunc::GetVector(const Baby &b) const{
  return vector_func_(b);
}

/*!\brief Add func to *this

  \param[in] func Function to be added to *this

  \return Reference to *this
*/
NamedFunc & NamedFunc::operator += (const NamedFunc &func){
  name_ = "("+name_ + ")+(" + func.name_ + ")";
  auto fp = ApplyOp(scalar_func_, vector_func_,
                    func.scalar_func_, func.vector_func_,
                    plus<ScalarType>());
  scalar_func_ = fp.first;
  vector_func_ = fp.second;
  return *this;
}

/*!\brief Subtract func from *this

  \param[in] func Function to be subtracted from *this

  \return Reference to *this
*/
NamedFunc & NamedFunc::operator -= (const NamedFunc &func){
  name_ = "("+name_ + ")-(" + func.name_ + ")";
  auto fp = ApplyOp(scalar_func_, vector_func_,
                    func.scalar_func_, func.vector_func_,
                    minus<ScalarType>());
  scalar_func_ = fp.first;
  vector_func_ = fp.second;
  return *this;
}

/*!\brief Multiply *this by func

  \param[in] func Function by which *this is multiplied

  \return Reference to *this
*/
NamedFunc & NamedFunc::operator *= (const NamedFunc &func){
  name_ = "("+name_ + ")*(" + func.name_ + ")";
  auto fp = ApplyOp(scalar_func_, vector_func_,
                    func.scalar_func_, func.vector_func_,
                    multiplies<ScalarType>());
  scalar_func_ = fp.first;
  vector_func_ = fp.second;
  return *this;
}

/*!\brief Divide *this by func

  \param[in] func Function by which to divide *this

  \return Reference to *this
*/
NamedFunc & NamedFunc::operator /= (const NamedFunc &func){
  name_ = "("+name_ + ")/(" + func.name_ + ")";
  auto fp = ApplyOp(scalar_func_, vector_func_,
                    func.scalar_func_, func.vector_func_,
                    divides<ScalarType>());
  scalar_func_ = fp.first;
  vector_func_ = fp.second;
  return *this;
}

/*!\brief Set *this to remainder of *this divided by func

  \param[in] func Function with respect to which to take remainder

  \return Reference to *this
*/
NamedFunc & NamedFunc::operator %= (const NamedFunc &func){
  name_ = "("+name_ + ")%(" + func.name_ + ")";
  auto fp = ApplyOp(scalar_func_, vector_func_,
                    func.scalar_func_, func.vector_func_,
                    static_cast<ScalarType (*)(ScalarType ,ScalarType)>(fmod));
  scalar_func_ = fp.first;
  vector_func_ = fp.second;
  return *this;
}

/*!\brief Apply indexing operator and return result as a NamedFunc
 */
NamedFunc NamedFunc::operator [] (const NamedFunc &func) const{
  if(IsScalar()) ERROR("Cannot apply indexing operator to scalar NamedFunc "+Name());
  if(func.IsVector()) ERROR("Cannot use vector "+func.Name()+" as index");
  const auto &vec = VectorFunction();
  const auto &index = func.ScalarFunction();
  return NamedFunc("("+Name()+")["+func.Name()+"]", [vec, index](const Baby &b){
      return vec(b).at(index(b));
    });
}

/*!\brief Strip spaces from name
 */
void NamedFunc::CleanName(){
  ReplaceAll(name_, " ", "");
}

/*!\brief Add two \link NamedFunc NamedFuncs\endlink

  \param[in] f Augend

  \param[in] g Addend

  \return NamedFunc which returns the sum of the results of f and g
*/
NamedFunc operator+(NamedFunc f, NamedFunc g){
  return f+=g;
}

/*!\brief Add a NamedFunc from another

  \param[in] f Minuend

  \param[in] g Subtrahend

  \return NamedFunc which returns the difference of the results of f and g
*/
NamedFunc operator-(NamedFunc f, NamedFunc g){
  return f-=g;
}

/*!\brief Multiply two \link NamedFunc NamedFuncs\endlink

  \param[in] f Multiplier

  \param[in] g Multiplicand

  \return NamedFunc which returns the product of the results of f and g
*/
NamedFunc operator*(NamedFunc f, NamedFunc g){
  return f*=g;
}

/*!\brief Divide two \link NamedFunc NamedFuncs\endlink

  \param[in] f Dividend

  \param[in] g Divisor

  \return NamedFunc which returns the quotient of the results of f and g
*/
NamedFunc operator/(NamedFunc f, NamedFunc g){
  return f/=g;
}

/*!\brief Get remainder form division of two \link NamedFunc NamedFuncs\endlink

  \param[in] f Dividend

  \param[in] g Divisor

  \return NamedFunc which returns the remainder from dividing the results of f
  and g
*/
NamedFunc operator%(NamedFunc f, NamedFunc g){
  return f%=g;
}

/*!\brief Applied unary plus operator. Acts as identity operation.

  \param[in] f NamedFunc to apply unary "+" to

  \return f
*/
NamedFunc operator + (NamedFunc f){
  f.Name("+(" + f.Name() + ")");
  return f;
}

/*!\brief Negates f

  \param[in] f NamedFunc to apply unary "-" to

  \return NamedFunc returing the negative of the result of f
*/
NamedFunc operator - (NamedFunc f){
  f.Name("-(" + f.Name() + ")");
  f.Function(ApplyOp(f.ScalarFunction(), negate<ScalarType>()));
  f.Function(ApplyOp(f.VectorFunction(), negate<ScalarType>()));
  return f;
}

/*!\brief Gets NamedFunc which tests for equality of results of f and g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f and g are equal
*/
NamedFunc operator == (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")==(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    equal_to<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests for inequality of results of f and g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f and g are not equal
*/
NamedFunc operator != (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")!=(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    not_equal_to<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if result of f is greater than result of g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f is greater than result of
  g
*/
NamedFunc operator > (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")>(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    greater<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if result of f is less than result of g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f is less than result of g
*/
NamedFunc operator < (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")<(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    less<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if result of f is greater than or equal to
  result of g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f is greater than or equal
  to result of g
*/
NamedFunc operator >= (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")>=(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    greater_equal<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if result of f is less than or equal to
  result of g

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f is less than or equal to
  result of g
*/
NamedFunc operator <= (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")<=(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    less_equal<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if results of both f and g are true

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of both f and g are true
*/
NamedFunc operator && (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")&&(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    logical_and<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunc which tests if result of f or g is true

  \param[in] f Left hand operand

  \param[in] g Right hand operand

  \return NamedFunc returning whether the results of f or g is true
*/
NamedFunc operator || (NamedFunc f, NamedFunc g){
  f.Name("(" + f.Name() + ")||(" + g.Name() + ")");
  auto fp = ApplyOp(f.ScalarFunction(), f.VectorFunction(),
                    g.ScalarFunction(), g.VectorFunction(),
                    logical_or<ScalarType>());
  f.Function(fp.first);
  f.Function(fp.second);
  return f;
}

/*!\brief Gets NamedFunct returning logical inverse of result of f

  \param[in] f Function whose result the logical not is applied to

  \return NamedFunc returning logical inverse of result of f
*/
NamedFunc operator ! (NamedFunc f){
  f.Name("!(" + f.Name() + ")");
  f.Function(ApplyOp(f.ScalarFunction(), logical_not<ScalarType>()));
  f.Function(ApplyOp(f.VectorFunction(), logical_not<ScalarType>()));
  return f;
}

/*!\brief Print NamedFunc to output stream

  \param[in,out] stream Output stream to print to

  \param[in] function NamedFunc to print
*/
ostream & operator<<(ostream &stream, const NamedFunc &function){
  stream << function.Name();
  return stream;
}

bool HavePass(const NamedFunc::VectorType &v){
  for(const auto &x: v){
    if(x) return true;
  }
  return false;
}

bool HavePass(const std::vector<NamedFunc::VectorType> &vv){
  if(vv.size()==0) return false;
  bool this_pass;
  for(size_t ix = 0; ix < vv.at(0).size(); ++ix){
    this_pass = true;
    for(size_t iv = 0; this_pass && iv < vv.size(); ++iv){
      if(ix>=vv.at(iv).size() || !vv.at(iv).at(ix)) this_pass = false;
    }
    if(this_pass) return true;
  }
  return false;
}
