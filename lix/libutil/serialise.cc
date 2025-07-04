#include "lix/libutil/serialise.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/signals.hh"

#include <cstring>
#include <cerrno>

namespace nix {

namespace {
/**
 * Convert a little-endian integer to host order.
 */
template<typename T>
T readLittleEndian(unsigned char * p)
{
    T x = 0;
    for (size_t i = 0; i < sizeof(x); ++i, ++p) {
        x |= ((T) *p) << (i * 8);
    }
    return x;
}
}

template<typename T>
T readNum(Source & source)
{
    unsigned char buf[8];
    source(charptr_cast<char *>(buf), sizeof(buf));

    auto n = readLittleEndian<uint64_t>(buf);

    if (n > (uint64_t) std::numeric_limits<T>::max())
        throw SerialisationError("serialised integer %d is too large for type '%s'", n, typeid(T).name());

    return (T) n;
}

template bool readNum<bool>(Source & source);

template unsigned char readNum<unsigned char>(Source & source);

template unsigned int readNum<unsigned int>(Source & source);

template unsigned long readNum<unsigned long>(Source & source);
template long readNum<long>(Source & source);

template unsigned long long readNum<unsigned long long>(Source & source);
template long long readNum<long long>(Source & source);

void BufferedSink::operator () (std::string_view data)
{
    while (!data.empty()) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (buffer->used() + data.size() >= buffer->size()) {
            flush();
            writeUnbuffered(data);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        auto into = buffer->getWriteBuffer();
        size_t n = std::min(data.size(), into.size());
        memcpy(into.data(), data.data(), n);
        data.remove_prefix(n);
        buffer->added(n);
        if (buffer->used() == buffer->size()) {
            flush();
        }
    }
}

void BufferedSink::flush()
{
    if (buffer->used() > 0) {
        auto from = buffer->getReadBuffer();
        writeUnbuffered({from.data(), from.size()});
        buffer->consumed(from.size());
    }
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreExceptionInDestructor(); }
}


void FdSink::writeUnbuffered(std::string_view data)
{
    try {
        writeFull(fd, data);
    } catch (SysError & e) {
        _good = false;
        throw;
    }
}

bool FdSink::good()
{
    return _good;
}


void Source::operator () (char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n; len -= n;
    }
}


void Source::drainInto(Sink & sink)
{
    std::array<char, 8192> buf;
    while (true) {
        size_t n;
        try {
            n = read(buf.data(), buf.size());
            sink({buf.data(), n});
        } catch (EndOfFile &) {
            break;
        }
    }
}


std::string Source::drain()
{
    StringSink s;
    drainInto(s);
    return std::move(s.s);
}

size_t BufferedSource::read(char * data, size_t len)
{
    if (buffer->used() == 0) {
        auto into = buffer->getWriteBuffer();
        buffer->added(readUnbuffered(into.data(), into.size()));
    }

    auto from = buffer->getReadBuffer();
    len = std::min(len, from.size());
    memcpy(data, from.data(), len);
    buffer->consumed(len);
    return len;
}


bool BufferedSource::hasData()
{
    return buffer->used() > 0;
}


size_t FdSource::readUnbuffered(char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, data, len);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        throw SysError("reading from file");
    }
    if (n == 0) {
        throw EndOfFile(endOfFileError());
    }
    return n;
}

std::string FdSource::endOfFileError() const
{
    return specialEndOfFileError.has_value() ? *specialEndOfFileError : "unexpected end-of-file";
}


size_t StringSource::read(char * data, size_t len)
{
    if (pos == s.size()) throw EndOfFile("end of string reached");
    size_t n = s.copy(data, len, pos);
    pos += n;
    return n;
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        char zero[8];
        memset(zero, 0, sizeof(zero));
        sink({zero, 8 - (len % 8)});
    }
}


WireFormatGenerator SerializingTransform::operator()(std::string_view s)
{
    co_yield s.size();
    co_yield Bytes(s.begin(), s.size());
    co_yield SerializingTransform::padding(s.size());
}

WireFormatGenerator SerializingTransform::operator()(const Strings & ss)
{
    co_yield ss.size();
    for (const auto & s : ss)
        co_yield std::string_view(s);
}

WireFormatGenerator SerializingTransform::operator()(const StringSet & ss)
{
    co_yield ss.size();
    for (const auto & s : ss)
        co_yield std::string_view(s);
}

WireFormatGenerator SerializingTransform::operator()(const Error & ex)
{
    auto & info = ex.info();
    co_yield "Error";
    co_yield info.level;
    co_yield "Error"; // removed
    co_yield info.msg.str();
    co_yield 0; // FIXME: info.errPos
    co_yield info.traces.size();
    for (auto & trace : info.traces) {
        co_yield 0; // FIXME: trace.pos
        co_yield trace.hint.str();
    }
}


void readPadding(size_t len, Source & source)
{
    if (len % 8) {
        char zero[8];
        size_t n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


size_t readString(char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}


std::string readString(Source & source, size_t max)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    std::string res(len, 0);
    source(res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator >> (Source & in, std::string & s)
{
    s = readString(in);
    return in;
}


template<class T> T readStrings(Source & source)
{
    auto count = readNum<size_t>(source);
    T ss;
    while (count--)
        ss.insert(ss.end(), readString(source));
    return ss;
}

template Paths readStrings(Source & source);
template PathSet readStrings(Source & source);


Error readError(Source & source)
{
    auto type = readString(source);
    assert(type == "Error");
    auto level = (Verbosity) readInt(source);
    readString(source); // removed (name)
    auto msg = readString(source);
    ErrorInfo info {
        .level = level,
        .msg = HintFmt(msg),
    };
    auto havePos = readNum<size_t>(source);
    assert(havePos == 0);
    auto nrTraces = readNum<size_t>(source);
    for (size_t i = 0; i < nrTraces; ++i) {
        havePos = readNum<size_t>(source);
        assert(havePos == 0);
        info.traces.push_back(Trace {
            .hint = HintFmt(readString(source))
        });
    }
    return Error(std::move(info));
}


void StringSink::operator () (std::string_view data)
{
    s.append(data);
}

}
