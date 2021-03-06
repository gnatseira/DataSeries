/*
  (c) Copyright 2003-2005, Hewlett-Packard Development Company, LP

  See the file named COPYING for license details
*/


#ifndef __EXTENT_SERIES_H
#define __EXTENT_SERIES_H
// This class allows us to have all the fields share a single
// dataseries so that you don't have to keep updating the extent
// associated with each field separately.  If you want to have fields
// use different extents, they need to use different data series.

class Field;
class Extent;

#include <boost/utility.hpp>

#include <Lintel/CompilerMarkup.hpp>
#include <Lintel/DebugFlag.hpp>

#include <DataSeries/ExtentType.hpp>
#include <DataSeries/Extent.hpp>
#include <DataSeries/SubExtentPointer.hpp>

// Allow a few of the files that define transitional functions to use the deprecated functions
// without a warning.
#ifndef DS_RAW_EXTENT_PTR_DEPRECATED
#define DS_RAW_EXTENT_PTR_DEPRECATED FUNC_DEPRECATED
#endif

/** \brief Encapsulates iteration over a group of similar Extents.
 *
 * Standard loop ends up being:
 * @code
 * for (series.start(e); series.more(); series.next()) { ... }
 * @endcode
 */
class ExtentSeries {
  public:
    // the types for the extents must match:
    //   typeExact: identical XML type descriptions
    //   TODO: typeMajorVersion: major version matches, may want to have
    //         a required major and a minimal minor specified also
    //   typeLoose: possibly reordered fields must match types (just
    //              the signature) for used fields
    /** Describes what variations in the types of Extents are allowed. */
    enum typeCompatibilityT {
        /** The ExtentSeries can only hold Extents of a single type. */
        typeExact,
        /** The ExtentSeries can hold Extents of any type which is 
            compatible with all of its fields.  This effectively means
            that it doesn't matter what the ExtentSeries held previously. */
        typeLoose
    };

    /** This constructor leaves the type unknown. The type can be set
        later either  explicitly using setType or implicitly using setExtent.

        Postconditions:
        - getType() == 0 and getExtent() == 0 */
    explicit ExtentSeries(typeCompatibilityT _tc = typeExact) 
            : type(), my_extent(NULL), typeCompatibility(_tc) {
    }

    /** This function is deprecated, you should switch to ExtentSeries(Ptr) */
    explicit ExtentSeries(const ExtentType &in_type, typeCompatibilityT _tc = typeExact) FUNC_DEPRECATED
            : type(in_type.shared_from_this()), my_extent(NULL), typeCompatibility(_tc) {
    }
    /** This function is deprecated, you should switch to ExtentSeries(Ptr) */
    explicit ExtentSeries(const ExtentType *in_type, typeCompatibilityT _tc = typeExact) FUNC_DEPRECATED
            : type(in_type->shared_from_this()), my_extent(NULL), typeCompatibility(_tc) {
    }

    /** Sets the type held by the ExtentSeries to the specified type.
        If the type is null, then this is equivalent to the default
        constructor.

        Postconditions:
        - getType() == _type and getExtent() == 0 */
    explicit ExtentSeries(const ExtentType::Ptr in_type, typeCompatibilityT _tc = typeExact)
            : type(in_type), my_extent(NULL), typeCompatibility(_tc) {
    }
    /** Sets the type held by the ExtentSeries by looking it up
        in an @c ExtentTypeLibrary

        Preconditions:
        - An ExtentType with the specified name has
        been registered with the library.

        Postconditions:
        - getType() == library.getTypeByName(type name)
        - getExtent() == 0 */
    ExtentSeries(ExtentTypeLibrary &library, std::string type_name,
                 typeCompatibilityT _tc = typeExact)
            : type(library.getTypeByNamePtr(type_name)), my_extent(NULL),
              typeCompatibility(_tc) {
    }

    /** Initializes with the specified @c Extent. If it is null then this
        is equivalent to the default constructor. Otherwise sets the
        type to the type of the @c Extent. */
    explicit ExtentSeries(Extent *e, typeCompatibilityT _tc = typeExact) DS_RAW_EXTENT_PTR_DEPRECATED;

    /** Initializes with the specified @c Extent. If it is null then this
        is equivalent to the default constructor. Otherwise sets the
        type to the type of the @c Extent. */
    explicit ExtentSeries(Extent::Ptr e, typeCompatibilityT _tc = typeExact);
                          
    /** Initialize using the @c ExtentType corresponding to the given XML. */
    explicit ExtentSeries(const std::string &xmltype, typeCompatibilityT tc = typeExact)
            : type(ExtentTypeLibrary::sharedExtentTypePtr(xmltype)),
              my_extent(NULL), typeCompatibility(tc) { 
    }

    /** Copy constructor */
    ExtentSeries(const ExtentSeries &from)
    : type(from.type), my_extent(from.my_extent), typeCompatibility(from.typeCompatibility),
      pos(from.pos), my_fields(from.my_fields)
    { }

    /** Copy operator */
    ExtentSeries &operator =(const ExtentSeries &rhs) {
        type = rhs.type;
        my_extent = rhs.my_extent;
        typeCompatibility = rhs.typeCompatibility;
        pos = rhs.pos;
        my_fields = rhs.my_fields;
        return *this;
    }


    /** Preconditions:
        - All the attached fields must have been destroyed first. */
    ~ExtentSeries();

    // Next three are the most common way of using a series; may
    // eventually decide to deprecate as many of the other options as
    // possible after investigating to see if they matter.

    /** Sets the current extent being processed. If e is null, clears
        the current Extent.  If the type has already been set, requires
        that the type of the new Extent be compatible with the existing
        type, as specified by the typeCompatibilityT argument of all the
        constructors. */
    void start(Extent *e) FUNC_DEPRECATED;

    /** Returns true iff the current extent is not null and has more records. */
    bool more() {
        return pos.morerecords();
    }
    /** Advances to the next record in the current Extent.

        Preconditions:
        - more() */
    void next() {
        ++pos;
    }

    // TODO: deprecate this function and setCurPos once we have some experience
    // with the SEP_* things.  This one is roughly equivalent to the direct row
    // pointer, but from a practical sense any of them should work.
    /** Together with @c setCurPos, this function allows saving and restoring
        the current position of an extent in a series.
        Returns an opaque handle to the current position in an Extent. The only
        thing that can be done with the result is to pass it to setCurPos. The
        handle is valid for any ExtentSeries which uses the same Extent. It is
        invalidated by any operation which changes the size of the Extent.

        Preconditions:
        - getExtent() != 0 */
    const void *getCurPos() {
        return pos.getPos();
    }

    /** Restores the current position to a saved state.  position must be the
        the result of a previous call to @c getCurPos() with the current
        @c Extent and must not have been invalidated.
        
        This function may be slow. (Slow means that it does a handful of
        arithmetic and comparisons to verify that the call leaves that
        the @c ExtentSeries in a valid state.) */
    void setCurPos(const void *position) {
        pos.setPos(position);
    }
    
    /** Sets the type of Extents. If the type has already been set in any way,
        requires that the new type be compatible with the existing type as
        specified by the typeCompatibilityT of all the constructors. */
    void setType(const ExtentType::Ptr type);

    /** Sets the type of Extents. If the type has already been set in any way,
        requires that the new type be compatible with the existing type as
        specified by the typeCompatibilityT of all the constructors. */
    void setType(const ExtentType &type) FUNC_DEPRECATED {
        setType(type.shared_from_this());
    }
    void setType(const ExtentType *type) FUNC_DEPRECATED {
        SINVARIANT(type != NULL);
        setType(type->shared_from_this());
    }



    /** Returns a pointer to the current type. If no type has been set, the
        result will be null.
        Invariants:
        - If getExtent() is not null, then the result is the
        same as getExtent()->type*/
    const ExtentType *getType() FUNC_DEPRECATED { return type.get(); }
    /** Returns a pointer to the current type. If no type has been set, the
        result will be null.
        Invariants:
        - If getExtent() is not null, then the result is the
        same as getExtent()->type*/
    const ExtentType::Ptr getTypePtr() { return type; }

    /** Sets the current extent being processed. If e is null, clears the
        current Extent.  If the type has already been set, requires that
        the type of the new Extent be compatible with the existing type.
        it is valid to call this with NULL which is equivalent to a call 
        to clearExtent()
        
        Postconditions:
        - getExtent() = e
        - the current position will be set to the beginning of the
        new @c Extent. */
    void setExtent(Extent *e) DS_RAW_EXTENT_PTR_DEPRECATED;

    // TODO: deprecate the non-shared pointer versions of series stuff; probably want to
    // wait until after the next release since we're already doing a big deprecation transition
    // for shared extents in modules.
    /** setExtent with a shared pointer */
    void setExtent(Extent::Ptr e);

    /** Equivalent to @c setExtent(&e) */
    void setExtent(Extent &e) DS_RAW_EXTENT_PTR_DEPRECATED;

    /** Clears the current Extent. Note that this only affects the
        Extent, the type is left unchanged.  Exactly equivalent to
        setExtent(NULL) */
    void clearExtent() { setExtent(Extent::Ptr()); }

    /** Creates a new, shared extent of the current type for the series and sets the series to that
        extent.  It is an error to call this function without a type assigned. */
    void newExtent();

    /** Returns the current extent. */
    Extent *extent() const DS_RAW_EXTENT_PTR_DEPRECATED { 
        return my_extent;
    }
    Extent *getExtent() const DS_RAW_EXTENT_PTR_DEPRECATED {
        return my_extent;
    }

    /** Returns the shared version of the extent, assuming that the extent was set that way; Note
        that you normally want getExtentRef() or hasExtent() */
    Extent::Ptr getSharedExtent() const {
        SINVARIANT(my_extent == shared_extent.get());
        return shared_extent;
    }

    /** Returns whether the series currently has an extent. */
    bool hasExtent() const {
        return shared_extent != NULL;
    }

    /** Returns a reference to the extent, aborts if no extent is set, or if the extent wasn't a
        shared extent when set. An error to call unless hasExtent() is true, but will work for
        now in non-debug builds. */
    Extent &getExtentRef() {
        DEBUG_SINVARIANT(hasExtent());
        return *my_extent;
    }

    /** Returns a constant reference to the extent, aborts if no extent is set, or if the extent
        wasn't a shared extent when set. An error to call unless hasExtent() is true, but will work
        for now in non-debug builds. */
    const Extent &getExtentRef() const {
        DEBUG_SINVARIANT(hasExtent());
        return *my_extent;
    }

    /** Registers the specified field. This should only be called from
        constructor of classes derived from @c Field.  Users should not need
        to call it. */
    void addField(Field &field);
    /** Removes a field. Never call this directly, the @c Field destructor
        handles this automatically.*/
    void removeField(Field &field, bool must_exist = true);

    /** Appends a record to the end of the current Extent. Invalidates any
        other @c ExtentSeries operating on the same Extent. i.e. you must
        restart the Extent in every such series.  The new record will contain
        all zeros. Variable32 fields will be empty. Nullable fields will be
        initialized just like non-nullable field--They will not be set to null.
        After this function returns, the current position will point to the
        new record.

        Preconditions:
        - The current Extent is not null. 

        \todo are multiple @c ExtentSeries even supported?
        \todo should nullable fields be set to null? */
    void newRecord() {
        INVARIANT(my_extent != NULL,
                  "must set extent for data series before calling newRecord()");
        size_t offset = my_extent->fixeddata.size();
        my_extent->createRecords(1);
        pos.cur_pos = my_extent->fixeddata.begin() + offset;
    }
    /** Appends a specified number of records onto the end of the current
        @c Extent.  The same cautions apply as for @c newRecord. The current
        record will be the first one inserted.

        Preconditions:
        - The current extent cannot be null */
    void createRecords(int nrecords) {
        // leaves the current record position unchanged
        INVARIANT(my_extent != NULL,
                  "must set extent for data series before calling newRecord()\n");
        size_t offset = pos.cur_pos - my_extent->fixeddata.begin();
        my_extent->createRecords(nrecords);
        pos.cur_pos = my_extent->fixeddata.begin() + offset;
    }
    /// \cond INTERNAL_ONLY
    // TODO: make this class go away, it doesn't actually make sense since
    // each of the fields are tied to the ExtentSeries, not to the iterator
    // within it.  Only use seems to be within Extent.C which is using the
    // raw access that the library has anyway.
    class iterator : boost::noncopyable {
      public:
        iterator() : cur_extent(NULL), cur_pos(NULL), recordsize(0) { }
        iterator(Extent *e) { reset(e); }
        iterator(const iterator &from) 
        : cur_extent(from.cur_extent), cur_pos(from.cur_pos), recordsize(from.recordsize)
        { }
        iterator &operator =(const iterator &rhs) {
            cur_extent = rhs.cur_extent;
            cur_pos = rhs.cur_pos;
            recordsize = rhs.recordsize;
            return *this;
        }

        typedef ExtentType::byte byte;
        iterator &operator++() { cur_pos += recordsize; return *this; }
        void reset(Extent *e) { 
            if (e == NULL) {
                cur_extent = NULL;
                cur_pos = NULL;
                recordsize = 0;
            } else {
                cur_extent = e;
                cur_pos = e->fixeddata.begin();
                recordsize = e->getTypePtr()->fixedrecordsize();
            }
        }
        
        /// old api
        void setpos(byte *new_pos) {
            setPos(new_pos);
        }
        /// old api
        byte *record_start() { return cur_pos; }

        void setPos(const void *new_pos);
        const void *getPos() {
            return cur_pos;
        }
        uint32_t currecnum() {
            SINVARIANT(cur_extent != NULL);
            int recnum = (cur_pos - cur_extent->fixeddata.begin()) / recordsize;
            checkOffset(cur_pos - cur_extent->fixeddata.begin());
            return recnum;
        }
        // You need to call update on any of your iterators after you
        // call Extent::createRecords() or ExtentSeries::newRecord().
        // newRecord() will update the series pos iterator. update()
        // keeps the current position at the same relative record as before
        void update(Extent *e);

        bool morerecords() {
            return cur_extent != NULL && cur_pos < cur_extent->fixeddata.end();
        }
        void checkOffset(long offset) {
#if LINTEL_DEBUG
            forceCheckOffset(offset);
#else
            (void)offset; // eliminate compilation warning
#endif
        }
        void forceCheckOffset(long offset);

      private:
        friend class ExtentSeries;
        Extent *cur_extent;
        byte *cur_pos;
        unsigned recordsize;
    };

    /// \endcond

    /** Returns true iff the current Extent is not null and we are not at the
        end of it. */
    bool morerecords() {
        return pos.morerecords();
    }

    /** Increment the current position of the series */
    iterator &operator++() { ++pos; return pos; }

    /** Verify that a specified offset is valid; checks are normally disabled except
        in debug mode */
    void checkOffset(long offset) {
        pos.checkOffset(offset);
    }

    /** Indicates how the ExtentSeries handles different Extent types. */
    typeCompatibilityT getTypeCompat() { return typeCompatibility; }
    /** Returns the current Extent. */
    const Extent *curExtent() FUNC_DEPRECATED { return my_extent; }

    dataseries::SEP_RowOffset getRowOffset() {
        DEBUG_SINVARIANT(pos.cur_extent != NULL
                         && pos.cur_pos >= pos.cur_extent->fixeddata.begin());
        return dataseries::SEP_RowOffset(pos.cur_pos - pos.cur_extent->fixeddata.begin(), 
                                         pos.cur_extent);
    }

  private:
    // both friends to get at pos.record_start()
    friend class Field;
    friend class ExtentRecordCopy;

    ExtentType::Ptr type;
    Extent *my_extent;
    Extent::Ptr shared_extent;
    typeCompatibilityT typeCompatibility;
    // TODO: we can probably remove the iterator; it doesn't really make sense, it was intended
    // to allow for random access, but worked sufficiently poorly that it wasn't ever used, and
    // the new approach makes the series == the current position, i.e. it is not separate.
    iterator pos;
    std::vector<Field *> my_fields;
};

#endif
