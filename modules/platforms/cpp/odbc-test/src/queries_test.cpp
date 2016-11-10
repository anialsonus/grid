/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef _WIN32
#   include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

#include <vector>
#include <string>
#include <algorithm>

#ifndef _MSC_VER
#   define BOOST_TEST_DYN_LINK
#endif

#include <boost/test/unit_test.hpp>

#include "ignite/ignite.h"
#include "ignite/ignition.h"
#include "ignite/impl/binary/binary_utils.h"

#include "test_type.h"
#include "test_utils.h"

using namespace ignite;
using namespace ignite::cache;
using namespace ignite::cache::query;
using namespace ignite::common;

using namespace boost::unit_test;

using ignite::impl::binary::BinaryUtils;

/**
 * Test setup fixture.
 */
struct QueriesTestSuiteFixture 
{
    /**
     * Establish connection to node.
     *
     * @param connectStr Connection string.
     */
    void Connect(const std::string& connectStr)
    {
        // Allocate an environment handle
        SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);

        BOOST_REQUIRE(env != NULL);

        // We want ODBC 3 support
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);

        // Allocate a connection handle
        SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);

        BOOST_REQUIRE(dbc != NULL);

        // Connect string
        std::vector<SQLCHAR> connectStr0;

        connectStr0.reserve(connectStr.size() + 1);
        std::copy(connectStr.begin(), connectStr.end(), std::back_inserter(connectStr0));

        SQLCHAR outstr[ODBC_BUFFER_SIZE];
        SQLSMALLINT outstrlen;

        // Connecting to ODBC server.
        SQLRETURN ret = SQLDriverConnect(dbc, NULL, &connectStr0[0], static_cast<SQLSMALLINT>(connectStr0.size()),
            outstr, sizeof(outstr), &outstrlen, SQL_DRIVER_COMPLETE);

        if (!SQL_SUCCEEDED(ret))
        {
            Ignition::Stop(grid.GetName(), true);

            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_DBC, dbc));
        }

        // Allocate a statement handle
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

        BOOST_REQUIRE(stmt != NULL);
    }

    void Disconnect()
    {
        // Releasing statement handle.
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);

        // Disconneting from the server.
        SQLDisconnect(dbc);

        // Releasing allocated handles.
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
    }

    static Ignite StartNode(const char* name, const char* config)
    {
        IgniteConfiguration cfg;

        cfg.jvmOpts.push_back("-Xdebug");
        cfg.jvmOpts.push_back("-Xnoagent");
        cfg.jvmOpts.push_back("-Djava.compiler=NONE");
        cfg.jvmOpts.push_back("-agentlib:jdwp=transport=dt_socket,server=y,suspend=n,address=5005");
        cfg.jvmOpts.push_back("-XX:+HeapDumpOnOutOfMemoryError");
        cfg.jvmOpts.push_back("-Duser.timezone=GMT");

#ifdef IGNITE_TESTS_32
        cfg.jvmInitMem = 256;
        cfg.jvmMaxMem = 768;
#else
        cfg.jvmInitMem = 1024;
        cfg.jvmMaxMem = 4096;
#endif

        char* cfgPath = getenv("IGNITE_NATIVE_TEST_ODBC_CONFIG_PATH");

        BOOST_REQUIRE(cfgPath != 0);

        cfg.springCfgPath.assign(cfgPath).append("/").append(config);

        IgniteError err;

        return Ignition::Start(cfg, name);
    }

    static Ignite StartAdditionalNode(const char* name)
    {
        return StartNode(name, "queries-test-noodbc.xml");
    }

    /**
     * Constructor.
     */
    QueriesTestSuiteFixture() : testCache(0), env(NULL), dbc(NULL), stmt(NULL)
    {
        grid = StartNode("NodeMain", "queries-test.xml");

        testCache = grid.GetCache<int64_t, TestType>("cache");
    }

    /**
     * Destructor.
     */
    ~QueriesTestSuiteFixture()
    {
        Disconnect();

        Ignition::StopAll(true);
    }

    template<typename T>
    void CheckTwoRowsInt(SQLSMALLINT type)
    {
        Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

        SQLRETURN ret;

        TestType in1(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
            BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

        TestType in2(8, 7, 6, 5, "4", 3.0f, 2.0, false, Guid(1, 0), BinaryUtils::MakeDateGmt(1976, 1, 12),
            BinaryUtils::MakeTimestampGmt(1978, 8, 21, 23, 13, 45, 456));

        testCache.Put(1, in1);
        testCache.Put(2, in2);

        const size_t columnsCnt = 11;

        T columns[columnsCnt] = { 0 };

        // Binding columns.
        for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
        {
            ret = SQLBindCol(stmt, i + 1, type, &columns[i], sizeof(columns[i]), 0);

            if (!SQL_SUCCEEDED(ret))
                BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
        }

        char request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
            "floatField, doubleField, boolField, guidField, dateField, timestampField FROM TestType";

        ret = SQLExecDirect(stmt, reinterpret_cast<SQLCHAR*>(request), SQL_NTS);
        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

        ret = SQLFetch(stmt);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

        BOOST_CHECK_EQUAL(columns[0], 1);
        BOOST_CHECK_EQUAL(columns[1], 2);
        BOOST_CHECK_EQUAL(columns[2], 3);
        BOOST_CHECK_EQUAL(columns[3], 4);
        BOOST_CHECK_EQUAL(columns[4], 5);
        BOOST_CHECK_EQUAL(columns[5], 6);
        BOOST_CHECK_EQUAL(columns[6], 7);
        BOOST_CHECK_EQUAL(columns[7], 1);
        BOOST_CHECK_EQUAL(columns[8], 0);
        BOOST_CHECK_EQUAL(columns[9], 0);
        BOOST_CHECK_EQUAL(columns[10], 0);

        SQLLEN columnLens[columnsCnt] = { 0 };

        // Binding columns.
        for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
        {
            ret = SQLBindCol(stmt, i + 1, type, &columns[i], sizeof(columns[i]), &columnLens[i]);

            if (!SQL_SUCCEEDED(ret))
                BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
        }

        ret = SQLFetch(stmt);
        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

        BOOST_CHECK_EQUAL(columns[0], 8);
        BOOST_CHECK_EQUAL(columns[1], 7);
        BOOST_CHECK_EQUAL(columns[2], 6);
        BOOST_CHECK_EQUAL(columns[3], 5);
        BOOST_CHECK_EQUAL(columns[4], 4);
        BOOST_CHECK_EQUAL(columns[5], 3);
        BOOST_CHECK_EQUAL(columns[6], 2);
        BOOST_CHECK_EQUAL(columns[7], 0);
        BOOST_CHECK_EQUAL(columns[8], 0);
        BOOST_CHECK_EQUAL(columns[9], 0);
        BOOST_CHECK_EQUAL(columns[10], 0);

        BOOST_CHECK_EQUAL(columnLens[0], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[1], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[2], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[3], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[4], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[5], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[6], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[7], static_cast<SQLLEN>(sizeof(T)));
        BOOST_CHECK_EQUAL(columnLens[8], SQL_NO_TOTAL);
        BOOST_CHECK_EQUAL(columnLens[9], SQL_NO_TOTAL);
        BOOST_CHECK_EQUAL(columnLens[10], SQL_NO_TOTAL);

        ret = SQLFetch(stmt);
        BOOST_CHECK(ret == SQL_NO_DATA);
    }

    /** Node started during the test. */
    Ignite grid;

    /** Test cache instance. */
    Cache<int64_t, TestType> testCache;

    /** ODBC Environment. */
    SQLHENV env;

    /** ODBC Connect. */
    SQLHDBC dbc;

    /** ODBC Statement. */
    SQLHSTMT stmt;
};

BOOST_FIXTURE_TEST_SUITE(QueriesTestSuite, QueriesTestSuiteFixture)

BOOST_AUTO_TEST_CASE(TestLegacyConnection)
{
    Connect("DRIVER={Apache Ignite};SERVER=127.0.0.1;PORT=11110;CACHE=cache");
}

BOOST_AUTO_TEST_CASE(TestConnectionProtocolVersion_1_6_0)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache;PROTOCOL_VERSION=1.6.0");
}

BOOST_AUTO_TEST_CASE(TestTwoRowsInt8)
{
    CheckTwoRowsInt<signed char>(SQL_C_STINYINT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsUint8)
{
    CheckTwoRowsInt<unsigned char>(SQL_C_UTINYINT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsInt16)
{
    CheckTwoRowsInt<signed short>(SQL_C_SSHORT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsUint16)
{
    CheckTwoRowsInt<unsigned short>(SQL_C_USHORT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsInt32)
{
    CheckTwoRowsInt<signed long>(SQL_C_SLONG);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsUint32)
{
    CheckTwoRowsInt<unsigned long>(SQL_C_ULONG);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsInt64)
{
    CheckTwoRowsInt<int64_t>(SQL_C_SBIGINT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsUint64)
{
    CheckTwoRowsInt<uint64_t>(SQL_C_UBIGINT);
}

BOOST_AUTO_TEST_CASE(TestTwoRowsString)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

    SQLRETURN ret;

    TestType in1(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
        BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

    TestType in2(8, 7, 6, 5, "4", 3.0f, 2.0, false, Guid(1, 0), BinaryUtils::MakeDateGmt(1976, 1, 12),
        BinaryUtils::MakeTimestampGmt(1978, 8, 21, 23, 13, 45, 999999999));

    testCache.Put(1, in1);
    testCache.Put(2, in2);

    const size_t columnsCnt = 11;

    SQLCHAR columns[columnsCnt][ODBC_BUFFER_SIZE] = { 0 };

    // Binding columns.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
    {
        ret = SQLBindCol(stmt, i + 1, SQL_C_CHAR, &columns[i], ODBC_BUFFER_SIZE, 0);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
    }

    SQLCHAR request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
        "floatField, doubleField, boolField, guidField, dateField, timestampField FROM TestType";

    ret = SQLExecDirect(stmt, request, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLFetch(stmt);

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[0])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[1])), "2");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[2])), "3");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[3])), "4");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[4])), "5");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[5])), "6");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[6])), "7");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[7])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[8])), "00000000-0000-0008-0000-000000000009");
    // Such format is used because Date returned as Timestamp.
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[9])), "1987-06-05 00:00:00");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[10])), "1998-12-27 01:02:03");

    SQLLEN columnLens[columnsCnt] = { 0 };

    // Binding columns.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
    {
        ret = SQLBindCol(stmt, i + 1, SQL_C_CHAR, &columns[i], ODBC_BUFFER_SIZE, &columnLens[i]);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
    }

    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[0])), "8");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[1])), "7");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[2])), "6");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[3])), "5");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[4])), "4");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[5])), "3");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[6])), "2");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[7])), "0");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[8])), "00000000-0000-0001-0000-000000000000");
    // Such format is used because Date returned as Timestamp.
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[9])), "1976-01-12 00:00:00");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[10])), "1978-08-21 23:13:45");

    BOOST_CHECK_EQUAL(columnLens[0], 1);
    BOOST_CHECK_EQUAL(columnLens[1], 1);
    BOOST_CHECK_EQUAL(columnLens[2], 1);
    BOOST_CHECK_EQUAL(columnLens[3], 1);
    BOOST_CHECK_EQUAL(columnLens[4], 1);
    BOOST_CHECK_EQUAL(columnLens[5], 1);
    BOOST_CHECK_EQUAL(columnLens[6], 1);
    BOOST_CHECK_EQUAL(columnLens[7], 1);
    BOOST_CHECK_EQUAL(columnLens[8], 36);
    BOOST_CHECK_EQUAL(columnLens[9], 19);
    BOOST_CHECK_EQUAL(columnLens[10], 19);

    ret = SQLFetch(stmt);
    BOOST_CHECK(ret == SQL_NO_DATA);
}

BOOST_AUTO_TEST_CASE(TestOneRowString)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

    SQLRETURN ret;

    TestType in(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
        BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

    testCache.Put(1, in);

    const size_t columnsCnt = 11;

    SQLCHAR columns[columnsCnt][ODBC_BUFFER_SIZE] = { 0 };

    SQLLEN columnLens[columnsCnt] = { 0 };

    // Binding columns.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
    {
        ret = SQLBindCol(stmt, i + 1, SQL_C_CHAR, &columns[i], ODBC_BUFFER_SIZE, &columnLens[i]);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
    }

    SQLCHAR request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
        "floatField, doubleField, boolField, guidField, dateField, timestampField FROM TestType";

    ret = SQLExecDirect(stmt, request, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[0])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[1])), "2");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[2])), "3");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[3])), "4");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[4])), "5");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[5])), "6");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[6])), "7");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[7])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[8])), "00000000-0000-0008-0000-000000000009");
    // Such format is used because Date returned as Timestamp.
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[9])), "1987-06-05 00:00:00");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[10])), "1998-12-27 01:02:03");

    BOOST_CHECK_EQUAL(columnLens[0], 1);
    BOOST_CHECK_EQUAL(columnLens[1], 1);
    BOOST_CHECK_EQUAL(columnLens[2], 1);
    BOOST_CHECK_EQUAL(columnLens[3], 1);
    BOOST_CHECK_EQUAL(columnLens[4], 1);
    BOOST_CHECK_EQUAL(columnLens[5], 1);
    BOOST_CHECK_EQUAL(columnLens[6], 1);
    BOOST_CHECK_EQUAL(columnLens[7], 1);
    BOOST_CHECK_EQUAL(columnLens[8], 36);
    BOOST_CHECK_EQUAL(columnLens[9], 19);
    BOOST_CHECK_EQUAL(columnLens[10], 19);

    ret = SQLFetch(stmt);
    BOOST_CHECK(ret == SQL_NO_DATA);
}

BOOST_AUTO_TEST_CASE(TestOneRowStringLen)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

    SQLRETURN ret;

    TestType in(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
        BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

    testCache.Put(1, in);

    const size_t columnsCnt = 11;

    SQLLEN columnLens[columnsCnt] = { 0 };

    // Binding columns.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
    {
        ret = SQLBindCol(stmt, i + 1, SQL_C_CHAR, 0, 0, &columnLens[i]);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
    }

    SQLCHAR request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
        "floatField, doubleField, boolField, guidField, dateField, timestampField FROM TestType";

    ret = SQLExecDirect(stmt, request, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    BOOST_CHECK_EQUAL(columnLens[0], 1);
    BOOST_CHECK_EQUAL(columnLens[1], 1);
    BOOST_CHECK_EQUAL(columnLens[2], 1);
    BOOST_CHECK_EQUAL(columnLens[3], 1);
    BOOST_CHECK_EQUAL(columnLens[4], 1);
    BOOST_CHECK_EQUAL(columnLens[5], 1);
    BOOST_CHECK_EQUAL(columnLens[6], 1);
    BOOST_CHECK_EQUAL(columnLens[7], 1);
    BOOST_CHECK_EQUAL(columnLens[8], 36);
    BOOST_CHECK_EQUAL(columnLens[9], 19);
    BOOST_CHECK_EQUAL(columnLens[10], 19);

    ret = SQLFetch(stmt);
    BOOST_CHECK(ret == SQL_NO_DATA);
}

BOOST_AUTO_TEST_CASE(TestDataAtExecution)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

    SQLRETURN ret;

    TestType in1(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
        BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

    TestType in2(8, 7, 6, 5, "4", 3.0f, 2.0, false, Guid(1, 0), BinaryUtils::MakeDateGmt(1976, 1, 12),
        BinaryUtils::MakeTimestampGmt(1978, 8, 21, 23, 13, 45, 999999999));

    testCache.Put(1, in1);
    testCache.Put(2, in2);

    const size_t columnsCnt = 11;

    SQLLEN columnLens[columnsCnt] = { 0 };
    SQLCHAR columns[columnsCnt][ODBC_BUFFER_SIZE] = { 0 };

    // Binding columns.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
    {
        ret = SQLBindCol(stmt, i + 1, SQL_C_CHAR, &columns[i], ODBC_BUFFER_SIZE, &columnLens[i]);

        if (!SQL_SUCCEEDED(ret))
            BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));
    }

    SQLCHAR request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
        "floatField, doubleField, boolField, guidField, dateField, timestampField FROM TestType "
        "WHERE i32Field = ? AND strField = ?";

    ret = SQLPrepare(stmt, request, SQL_NTS);

    SQLLEN ind1 = 1;
    SQLLEN ind2 = 2;

    SQLLEN len1 = SQL_DATA_AT_EXEC;
    SQLLEN len2 = SQL_LEN_DATA_AT_EXEC(static_cast<SQLLEN>(in1.strField.size()));

    ret = SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 100, 100, &ind1, sizeof(ind1), &len1);

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 100, 100, &ind2, sizeof(ind2), &len2);

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLExecute(stmt);

    BOOST_REQUIRE_EQUAL(ret, SQL_NEED_DATA);

    void* oind;

    ret = SQLParamData(stmt, &oind);

    BOOST_REQUIRE_EQUAL(ret, SQL_NEED_DATA);

    if (oind == &ind1)
        ret = SQLPutData(stmt, &in1.i32Field, 0);
    else if (oind == &ind2)
        ret = SQLPutData(stmt, (SQLPOINTER)in1.strField.c_str(), (SQLLEN)in1.strField.size());
    else
        BOOST_FAIL("Unknown indicator value");

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLParamData(stmt, &oind);

    BOOST_REQUIRE_EQUAL(ret, SQL_NEED_DATA);

    if (oind == &ind1)
        ret = SQLPutData(stmt, &in1.i32Field, 0);
    else if (oind == &ind2)
        ret = SQLPutData(stmt, (SQLPOINTER)in1.strField.c_str(), (SQLLEN)in1.strField.size());
    else
        BOOST_FAIL("Unknown indicator value");

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLParamData(stmt, &oind);

    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[0])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[1])), "2");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[2])), "3");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[3])), "4");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[4])), "5");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[5])), "6");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[6])), "7");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[7])), "1");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[8])), "00000000-0000-0008-0000-000000000009");
    // Such format is used because Date returned as Timestamp.
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[9])), "1987-06-05 00:00:00");
    BOOST_CHECK_EQUAL(std::string(reinterpret_cast<char*>(columns[10])), "1998-12-27 01:02:03");

    BOOST_CHECK_EQUAL(columnLens[0], 1);
    BOOST_CHECK_EQUAL(columnLens[1], 1);
    BOOST_CHECK_EQUAL(columnLens[2], 1);
    BOOST_CHECK_EQUAL(columnLens[3], 1);
    BOOST_CHECK_EQUAL(columnLens[4], 1);
    BOOST_CHECK_EQUAL(columnLens[5], 1);
    BOOST_CHECK_EQUAL(columnLens[6], 1);
    BOOST_CHECK_EQUAL(columnLens[7], 1);
    BOOST_CHECK_EQUAL(columnLens[8], 36);
    BOOST_CHECK_EQUAL(columnLens[9], 19);
    BOOST_CHECK_EQUAL(columnLens[10], 19);

    ret = SQLFetch(stmt);
    BOOST_CHECK(ret == SQL_NO_DATA);
}

BOOST_AUTO_TEST_CASE(TestNullFields)
{
    Connect("DRIVER={Apache Ignite};ADDRESS=127.0.0.1:11110;CACHE=cache");

    SQLRETURN ret;

    TestType in(1, 2, 3, 4, "5", 6.0f, 7.0, true, Guid(8, 9), BinaryUtils::MakeDateGmt(1987, 6, 5),
        BinaryUtils::MakeTimestampGmt(1998, 12, 27, 1, 2, 3, 456));

    TestType inNull;

    inNull.allNulls = true;

    testCache.Put(1, in);
    testCache.Put(2, inNull);
    testCache.Put(3, in);

    const size_t columnsCnt = 10;

    SQLLEN columnLens[columnsCnt] = { 0 };

    int8_t i8Column;
    int16_t i16Column;
    int32_t i32Column;
    int64_t i64Column;
    char strColumn[ODBC_BUFFER_SIZE];
    float floatColumn;
    double doubleColumn;
    bool boolColumn;
    SQL_DATE_STRUCT dateColumn;
    SQL_TIMESTAMP_STRUCT timestampColumn;

    // Binding columns.
    ret = SQLBindCol(stmt, 1, SQL_C_STINYINT, &i8Column, 0, &columnLens[0]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 2, SQL_C_SSHORT, &i16Column, 0, &columnLens[1]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 3, SQL_C_SLONG, &i32Column, 0, &columnLens[2]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 4, SQL_C_SBIGINT, &i64Column, 0, &columnLens[3]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 5, SQL_C_CHAR, &strColumn, ODBC_BUFFER_SIZE, &columnLens[4]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 6, SQL_C_FLOAT, &floatColumn, 0, &columnLens[5]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 7, SQL_C_DOUBLE, &doubleColumn, 0, &columnLens[6]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 8, SQL_C_BIT, &boolColumn, 0, &columnLens[7]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 9, SQL_C_DATE, &dateColumn, 0, &columnLens[8]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    ret = SQLBindCol(stmt, 10, SQL_C_TIMESTAMP, &timestampColumn, 0, &columnLens[9]);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    SQLCHAR request[] = "SELECT i8Field, i16Field, i32Field, i64Field, strField, "
        "floatField, doubleField, boolField, dateField, timestampField FROM TestType ORDER BY _key";

    ret = SQLExecDirect(stmt, request, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    // Fetching the first non-null row.
    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    // Checking that columns are not null.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
        BOOST_CHECK_NE(columnLens[i], SQL_NULL_DATA);

    // Fetching null row.
    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    // Checking that columns are null.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
        BOOST_CHECK_EQUAL(columnLens[i], SQL_NULL_DATA);

    // Fetching the last non-null row.
    ret = SQLFetch(stmt);
    if (!SQL_SUCCEEDED(ret))
        BOOST_FAIL(GetOdbcErrorMessage(SQL_HANDLE_STMT, stmt));

    // Checking that columns are not null.
    for (SQLSMALLINT i = 0; i < columnsCnt; ++i)
        BOOST_CHECK_NE(columnLens[i], SQL_NULL_DATA);

    ret = SQLFetch(stmt);
    BOOST_CHECK(ret == SQL_NO_DATA);
}

BOOST_AUTO_TEST_SUITE_END()
