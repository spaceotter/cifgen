# unplusplus

This project aims to generate from a C++ header a C++ library containing stubs with C linkage that
allow basic usage of C++ features from C.

## Workflow:
1. Build the target C++ library.
2. Create a header with all the desired parts of the target library included.
3. Run unplusplus on that header to generate the C++ to C interface.
4. Build the generated stubs with the C++ compiler.
5. Build the C code that uses the generated header with the C compiler.

It is recommended to use the clang to compile the project, whose libclang unplusplus was built with,
to avoid issues with system header or ABI incompatibility.

## Generated Code

* If the C++ code includes headers that are known to be C system headers, all the symbols are
  skipped and that header is included.
* `extern "C"` symbols will be declared, but not defined.
* Namespaces and templates are collapsed according to a name-mangling system. For instance,
  `A::foo<int>` becomes `upp_A_foo_int`
* It will try to fully instantiate template specializations that were hinted at in the supplied
  code.
* Redundant code will not be emitted for `using` declarations.
* Constructors will generate a function that allocates a new object.
* Destructors will generate a function that frees an object.
* Class methods will generate a function that invokes the method on an object.
* Overridden functions will be given a unique name by appending a number.
* Any passing of structs/classes by value or C++ `&` reference are changed to passing by pointer,
  for the best compatibility with FFI.
* Returning structs/classes by value is changed to supplying a pointer argument for the output to be
  assigned.

## Limitations

Many C++ features are not yet supported. Member access is planned, possibly direct access to vtables
or some other way to override virtual methods.
