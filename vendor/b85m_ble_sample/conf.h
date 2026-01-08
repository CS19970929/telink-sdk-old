#ifndef CONF_H_
#define CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "../../common/types.h"
#include "stdint.h"

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

#define __INIT_SOC__        (100)

#define SeriesNum  (10)

#define FAC_INIT_soc (60)
// #define CapacityFactory (87)
#define CapacityFactory (50)

typedef enum _CUR {
CurCHG = 0, CurDSG
}_Cur;

#define UPDNLMT16(Var,Max,Min)	{(Var)=((Var)>=(Max))?(Max):(Var);(Var)=((Var)<=(Min))?(Min):(Var);}

#define Feed_IWatchDog ;
#define log_i(...)   ;


// typedef const int32_t sc32;  /*!< Read Only */
// typedef const int16_t sc16;  /*!< Read Only */
// typedef const int8_t sc8;   /*!< Read Only */

// typedef __IO int32_t  vs32;
// typedef __IO int16_t  vs16;
// typedef __IO int8_t   vs8;

// typedef __I int32_t vsc32;  /*!< Read Only */
// typedef __I int16_t vsc16;  /*!< Read Only */
// typedef __I int8_t vsc8;   /*!< Read Only */

#endif
