#ifndef VOIDC_DLLEXPORT_H
#define VOIDC_DLLEXPORT_H


//---------------------------------------------------------------------
#ifdef _WIN32

#define VOIDC_DLLEXPORT_BEGIN \
_Pragma("clang attribute push ([[gnu::dllexport]], apply_to=any(function, variable))")

#define VOIDC_DLLEXPORT_END \
_Pragma("clang attribute pop")

#endif

#ifdef __unix

#define VOIDC_DLLEXPORT_BEGIN

#define VOIDC_DLLEXPORT_END

#endif



#endif  //- VOIDC_DLLEXPORT_H
