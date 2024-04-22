/*
 * Memory BandWidth Partitioner.
 */

// ------------------------------------------------------------------------
INTERFACE:

class Mbwp
{
public:
  static void init();
  static void handle_period();
};

// ------------------------------------------------------------------------
IMPLEMENTATION:

PUBLIC
Mbwp::Mbwp()
{}

IMPLEMENT_DEFAULT static
void
Mbwp::init()
{}

IMPLEMENT_DEFAULT static
void
Mbwp::handle_period()
{}

