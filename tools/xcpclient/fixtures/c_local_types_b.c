// C test fixture for the xcpclient unit tests, second compilation unit, see c_local_types_a.c
struct state {
    unsigned int b1;
    unsigned int b2;
};
struct state state_b = {2, 3};

unsigned int get_state_b(void) { return state_b.b2; }
