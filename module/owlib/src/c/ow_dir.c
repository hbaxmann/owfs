/*
$Id$
    OWFS -- One-Wire filesystem
    OWHTTPD -- One-Wire Web Server
    Written 2003 Paul H Alfille
    email: palfille@earthlink.net
    Released under the GPL
    See the header file: ow.h for full attribution
    1wire/iButton system from Dallas Semiconductor
*/

#include "owfs_config.h"
#include "ow_devices.h"

static int FS_branchoff( const struct parsedname * const pn) ;
static int FS_devdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) ;
static int FS_alarmdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) ;
static int FS_typedir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) ;
static int FS_realdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) ;
static int FS_cache2real( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) ;
static void loadpath( struct parsedname * const pn2 ) ;
enum deviceformat devform = fdi ;

/* Calls dirfunc() for each element in directory */
/* void * data is arbitrary user data passed along -- e.g. output file descriptor */
/* pn -- input:
    pn->dev == NULL -- root directory, give list of all devices
    pn->dev non-null, -- device directory, give all properties
    branch aware
    cache aware

   pn -- output (with each call to dirfunc)
    ROOT
    pn->dev set
    pn->sn set appropriately
    pn->ft not set

    DEVICE
    pn->dev and pn->sn still set
    pn->ft loops through
*/

int FS_dir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, const struct parsedname * const pn ) {
    int ret = 0 ;
    struct parsedname pn2 ;

    STATLOCK
        AVERAGE_IN(&dir_avg)
        AVERAGE_IN(&all_avg)
    STATUNLOCK
    FSTATLOCK
        dir_time = time(NULL) ; // protected by mutex
    FSTATUNLOCK
    /* Make a copy (shallow) of pn to modify for directory entries */
    memcpy( &pn2, pn , sizeof( struct parsedname ) ) ; /*shallow copy */
//printf("DIR\n");
    if ( pn == NULL ) {
        ret = -ENOENT ; /* should ever happen */
    } else if ( pn->dev ){ /* device directory */
        ret = FS_devdir( dirfunc, data, &pn2 ) ;
    } else if ( pn->state == pn_alarm ) {  /* root or branch directory -- alarm state */
        ret = FS_alarmdir( dirfunc, data, &pn2 ) ;
    } else if ( pn->type != pn_real ) {  /* stat, sys or set dir */
        ret = FS_typedir( dirfunc, data, &pn2 ) ;
    } else if ( pn->state == pn_uncached ) {
        ret = FS_realdir( dirfunc, data, &pn2 ) ;
    } else {  /* root or branch directory -- non-alarm */

        /* Show alarm and uncached and stats... (if root directory) */
        if ( pn2.pathlength == 0 ) { /* true root */
            pn2.state = pn_alarm ;
            dirfunc( data, &pn2 ) ;
            pn2.state = pn->state ;
            if ( cacheenabled ) { /* cached */
                pn2.state = pn_uncached ;
                dirfunc( data, &pn2 ) ;
                pn2.state = pn_normal ;
            }
            pn2.type = pn_settings ;
            dirfunc( data, &pn2 ) ;
            pn2.type = pn_system ;
            dirfunc( data, &pn2 ) ;
            pn2.type = pn_statistics ;
            dirfunc( data, &pn2 ) ;
            pn2.type = pn_structure ;
            dirfunc( data, &pn2 ) ;
            pn2.type = pn_real ;
        }

        /* Show all devices in this directory */
        if ( cacheenabled && timeout.dir ) {
            FS_cache2real( dirfunc, data, &pn2 ) ;
        } else {
            FS_realdir( dirfunc, data, &pn2 ) ;
        }
    }
    STATLOCK
        AVERAGE_OUT(&dir_avg)
        AVERAGE_OUT(&all_avg)
    STATUNLOCK
    return ret ;
}

/* Device directory -- all from memory */
static int FS_devdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) {
    struct filetype * lastft = & (pn2->dev->ft[pn2->dev->nft]) ; /* last filetype struct */
    struct filetype * firstft ; /* first filetype struct */
    char s[33] ;
    int len ;

    STATLOCK
        ++dir_dev.calls ;
    STATUNLOCK
    if ( pn2->subdir ) { /* indevice subdir, name prepends */
//printf("DIR device subdirectory\n");
        strcpy( s , pn2->subdir->name ) ;
        strcat( s , "/" ) ;
        len = strlen(s) ;
        firstft = pn2->subdir  + 1 ;
    } else {
//printf("DIR device directory\n");
        len = 0 ;
        firstft = pn2->dev->ft ;
    }
    for ( pn2->ft=firstft ; pn2->ft < lastft ; ++pn2->ft ) { /* loop through filetypes */
        if ( len ) { /* subdir */
            /* test start of name for directory name */
            if ( strncmp( pn2->ft->name , s , len ) ) break ;
        } else { /* primary device directory */
            if ( strchr( pn2->ft->name, '/' ) ) continue ;
        }
        if ( pn2->ft->ag ) {
            for ( pn2->extension=-1 ; pn2->extension < pn2->ft->ag->elements ; ++pn2->extension ) {
                dirfunc( data, pn2 ) ;
                STATLOCK
                    ++dir_dev.entries ;
                STATUNLOCK
            }
        } else {
            pn2->extension = 0 ;
            dirfunc( data, pn2 ) ;
            STATLOCK
                ++dir_dev.entries ;
            STATUNLOCK
        }
    }
    return 0 ;
}

/* Note -- alarm directory is smaller, no adapters or stats or uncached */
static int FS_alarmdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) {
    int ret ;
    unsigned char sn[8] ;

    /* STATISCTICS */
    STATLOCK
        ++dir_main.calls ;
    STATUNLOCK

    pn2->ft = NULL ; /* just in case not properly set */
    BUSLOCK
    /* Turn off all DS2409s */
    FS_branchoff(pn2) ;
    (ret=BUS_select(pn2)) || (ret=BUS_first_alarm(sn,pn2)) ;
    while (ret==0) {
        char ID[] = "XX";
        STATLOCK
            ++dir_main.entries ;
        STATUNLOCK
        num2string( ID, sn[0] ) ;
        memcpy( pn2->sn, sn, 8 ) ;
        /* Search for known 1-wire device -- keyed to device name (family code in HEX) */
        FS_devicefind( ID, pn2 ) ;
        dirfunc( data, pn2 ) ;
        pn2->dev = NULL ; /* clear for the rest of directory listing */
        (ret=BUS_select(pn2)) || (ret=BUS_next_alarm(sn,pn2)) ;
    }
    BUSUNLOCK
    return ret ;
}

static int FS_branchoff( const struct parsedname * const pn ) {
    int ret ;
    unsigned char cmd[] = { 0xCC, 0x66, } ;

    /* Turn off all DS2409s */
    if ( (ret=BUS_reset(pn)) || (ret=BUS_send_data(cmd,2)) ) return ret ;
    return ret ;
}

/* A directory of devices -- either main or branch */
/* not within a device, nor alarm state */
/* Also, adapters and stats handled elsewhere */
static int FS_realdir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) {
    unsigned char sn[8] ;
    int dindex = 0 ;
    int ret ;

    /* STATISCTICS */
    STATLOCK
        ++dir_main.calls ;
    STATUNLOCK

    BUSLOCK
    /* Turn off all DS2409s */
    FS_branchoff(pn2) ;

//printf("DIR pathlength=%d, sn=%.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X ft=%p \n",pn2->pathlength,pn2->sn[0],pn2->sn[1],pn2->sn[2],pn2->sn[3],pn2->sn[4],pn2->sn[5],pn2->sn[6],pn2->sn[7],pn2->ft);
    (ret=BUS_select(pn2)) || (ret=BUS_first(sn,pn2)) ;
    while (ret==0) {
        char ID[] = "XX";
        STATLOCK
            ++dir_main.entries ;
        STATUNLOCK
        loadpath( pn2 ) ;
        Cache_Add_Dir(sn,dindex,pn2) ;
        ++dindex ;
        num2string( ID, sn[0] ) ;
        memcpy( pn2->sn, sn, 8 ) ;
        /* Search for known 1-wire device -- keyed to device name (family code in HEX) */
        FS_devicefind( ID, pn2 ) ;
        dirfunc( data, pn2 ) ;
        pn2->dev = NULL ; /* clear for the rest of directory listing */
        (ret=BUS_select(pn2)) || (ret=BUS_next(sn,pn2)) ;
    }
    BUSUNLOCK
    Cache_Del_Dir(dindex,pn2) ;
    return 0 ;
}

static void loadpath( struct parsedname * const pn ) {
    if ( pn->pathlength==0 ) {
        memset(pn->sn,0,8) ;
    } else {
        memcpy( pn->sn,pn->bp[pn->pathlength-1].sn,7) ;
        pn->sn[7] = pn->bp[pn->pathlength-1].branch ;
    }
}

/* A directory of devices -- either main or branch */
/* not within a device, nor alarm state */
/* Also, adapters and stats handled elsewhere */
static int FS_cache2real( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) {
    unsigned char sn[8] , snpath[8] ;
    int dindex = 0 ;

    loadpath(pn2) ;
    memcpy(snpath,pn2->sn,8);
    if ( pn2->state==pn_uncached || Cache_Get_Dir(sn,0,pn2 ) )
        return FS_realdir(dirfunc,data,pn2) ;

    /* STATISCTICS */
    STATLOCK
        ++dir_main.calls ;
    STATUNLOCK

    do {
        char ID[] = "XX";
        num2string( ID, sn[0] ) ;
        memcpy( pn2->sn, sn, 8 ) ;
        /* Search for known 1-wire device -- keyed to device name (family code in HEX) */
        FS_devicefind( ID, pn2 ) ;
        dirfunc( data, pn2 ) ;
        pn2->dev = NULL ; /* clear for the rest of directory listing */
        memcpy(pn2->sn,snpath,8) ;
        ++dindex ;
    } while ( Cache_Get_Dir( sn, dindex, pn2 )==0 ) ;
    STATLOCK
        dir_main.entries += dindex ;
    STATUNLOCK
    return 0 ;
}

/* Show the pn->type (statistics, system, ...) entries */
/* Only the top levels, the rest will be shown by FS_devdir */
static int FS_typedir( void (* dirfunc)(void *,const struct parsedname * const), void * const data, struct parsedname * const pn2 ) {
    enum pn_type type = pn2->type ;
    void action( const void * t, const VISIT which, const int depth ) {
        (void) depth ;
//printf("Action\n") ;
        switch(which) {
        case leaf:
        case postorder:
            pn2->dev = ((const struct device_opaque *)t)->key ;
            dirfunc(data, pn2 ) ;
        default:
            break ;
        }
    } ;
    if (type==pn_structure) type = pn_real ; // to go through "real" devices
    twalk( Tree[type],action) ;
    pn2->dev = NULL ;
    return 0 ;
}

/* device display format */
void FS_devicename( char * const buffer, const size_t length, const unsigned char * const sn ) {
    switch (devform) {
    case fdi:
        snprintf( buffer , length, "%02X.%02X%02X%02X%02X%02X%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6]) ;
        break ;
    case fi:
        snprintf( buffer , length, "%02X%02X%02X%02X%02X%02X%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6]) ;
        break ;
    case fdidc:
        snprintf( buffer , length, "%02X.%02X%02X%02X%02X%02X%02X.%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]) ;
        break ;
    case fdic:
        snprintf( buffer , length, "%02X.%02X%02X%02X%02X%02X%02X%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]) ;
        break ;
    case fidc:
        snprintf( buffer , length, "%02X%02X%02X%02X%02X%02X%02X.%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]) ;
        break ;
    case fic:
        snprintf( buffer , length, "%02X%02X%02X%02X%02X%02X%02X%02X",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]) ;
        break ;
    }
}

char * FS_dirname_state( const enum pn_state state ) {
    switch (state) {
    case pn_uncached:
        return "uncached";
    case pn_alarm:
        return "alarm";
    default:
        return "" ;
    }
}

char * FS_dirname_type( const enum pn_type type ) {
    switch (type) {
    case pn_statistics:
        return "statistics";
    case pn_system:
        return "system";
    case pn_settings:
        return "settings";
    case pn_structure:
        return "structure";
    default:
        return "" ;
    }
}

int FS_FileName( char * name, const size_t size, const struct parsedname * pn ) {
    int s ;
    if ( pn->ft == NULL ) return -ENOENT ;
    if ( pn->ft->ag == NULL ) {
        s = snprintf( name , size, "%s",pn->ft->name) ;
    } else if ( pn->extension == -1 ) {
        s = snprintf( name , size, "%s.ALL",pn->ft->name) ;
    } else if ( pn->ft->ag->letters == ag_letters ) {
        s = snprintf( name , size, "%s.%c",pn->ft->name,pn->extension+'A') ;
    } else {
        s = snprintf( name , size, "%s.%-d",pn->ft->name,pn->extension) ;
    }
    return (s<0) ? -ENOBUFS : 0 ;
}

/* Return the last part of the file name specified by pn */
/* Prints this directory element (not the whole path) */
/* Suggest that size = OW_FULLNAME_MAX */
void FS_DirName( char * buffer, const size_t size, const struct parsedname * const pn ) {
    if ( pn->ft ) { /* A real file! */
        char * pname = strchr(pn->ft->name,'/') ; // for subdirectories
        if ( pname ) {
            ++ pname ;
        } else {
            pname = pn->ft->name ;
        }

        if ( pn->ft->ag == NULL ) {
            snprintf( buffer , size, "%s",pname) ;
        } else if ( pn->extension == -1 ) {
            snprintf( buffer , size, "%s.ALL",pname) ;
        } else if ( pn->ft->ag->letters == ag_letters ) {
            snprintf( buffer , size, "%s.%c",pname,pn->extension+'A') ;
        } else {
            snprintf( buffer , size, "%s.%-d",pname,pn->extension) ;
        }
    } else if ( pn->subdir ) { /* in-device subdirectory */
        strncpy( buffer, pn->subdir->name, size) ;
    } else if (pn->dev == NULL ) { /* root-type directory */
        if ( pn->state != pn_normal ) {
            strncpy( buffer, FS_dirname_state( pn->state ), size ) ;
        } else {
            strncpy( buffer, FS_dirname_type( pn->type ), size ) ;
        }
    } else if ( pn->type == pn_real ) {
        FS_devicename( buffer, size, pn->sn ) ;
    } else {
        strncpy( buffer, pn->dev->code, size ) ;
    }
}
