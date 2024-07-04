#ifndef __SPC_COMMON_H__
#define __SPC_COMMON_H__

#include <cstdint>

inline unsigned get_le16( void const* p )
{
 return (unsigned) ((unsigned char const*) p) [1] << 8 |
   (unsigned) ((unsigned char const*) p) [0];
}
inline unsigned get_be16( void const* p )
{
 return (unsigned) ((unsigned char const*) p) [0] << 8 |
   (unsigned) ((unsigned char const*) p) [1];
}
inline uint32_t get_le32( void const* p )
{
 return (uint32_t) ((unsigned char const*) p) [3] << 24 |
   (uint32_t) ((unsigned char const*) p) [2] << 16 |
   (uint32_t) ((unsigned char const*) p) [1] << 8 |
   (uint32_t) ((unsigned char const*) p) [0];
}
inline uint32_t get_be32( void const* p )
{
 return (uint32_t) ((unsigned char const*) p) [0] << 24 |
   (uint32_t) ((unsigned char const*) p) [1] << 16 |
   (uint32_t) ((unsigned char const*) p) [2] << 8 |
   (uint32_t) ((unsigned char const*) p) [3];
}
inline void set_le16( void* p, unsigned n )
{
 ((unsigned char*) p) [1] = (unsigned char) (n >> 8);
 ((unsigned char*) p) [0] = (unsigned char) n;
}
inline void set_be16( void* p, unsigned n )
{
 ((unsigned char*) p) [0] = (unsigned char) (n >> 8);
 ((unsigned char*) p) [1] = (unsigned char) n;
}
inline void set_le32( void* p, uint32_t n )
{
 ((unsigned char*) p) [0] = (unsigned char) n;
 ((unsigned char*) p) [1] = (unsigned char) (n >> 8);
 ((unsigned char*) p) [2] = (unsigned char) (n >> 16);
 ((unsigned char*) p) [3] = (unsigned char) (n >> 24);
}
inline void set_be32( void* p, uint32_t n )
{
 ((unsigned char*) p) [3] = (unsigned char) n;
 ((unsigned char*) p) [2] = (unsigned char) (n >> 8);
 ((unsigned char*) p) [1] = (unsigned char) (n >> 16);
 ((unsigned char*) p) [0] = (unsigned char) (n >> 24);
}
inline void set_le( uint16_t* p, unsigned n ) { set_le16( p, n ); }
inline void set_le( uint32_t* p, uint32_t n ) { set_le32( p, n ); }
inline void set_be( uint16_t* p, unsigned n ) { set_be16( p, n ); }
inline void set_be( uint32_t* p, uint32_t n ) { set_be32( p, n ); }
inline unsigned get_le( uint16_t* p ) { return get_le16( p ); }
inline uint32_t get_le( uint32_t* p ) { return get_le32( p ); }
inline unsigned get_be( uint16_t* p ) { return get_be16( p ); }
inline uint32_t get_be( uint32_t* p ) { return get_be32( p ); }


#endif
