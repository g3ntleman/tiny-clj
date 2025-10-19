void test_autorelease_pool() {
    WITH_AUTORELEASE_POOL({
        CljObject *obj = make_string("test");
        AUTORELEASE(obj);
        printf("Test with autorelease pool
");
    });
}
