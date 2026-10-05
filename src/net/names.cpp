#include "names.h"

#include <cstdio>
#include <cstring>

namespace names {

namespace {

// APPEND-ONLY (see names.h): new words go at the end of a list. Frozen
// from v0.22.0 (Tom's review, 2026-10-04: every pair checked; these were
// taken out for slang, innuendo, slurs or bullying meanings, and must not
// come back: Soggy, Floppy, Secret, Stormy, Swirly, Zigzag; Pickle, Taco,
// Muffin, Banana, Zucchini, Carrot, Rocket, Trombone, Doodle, Monkey,
// Coconut, Raccoon).
const char* const kFirst[] = {
    "Wobbly",   "Turbo",    "Fuzzy",    "Sneaky",   "Bouncy",   "Giggly",   "Sleepy",   "Grumpy",
    "Sparkly",  "Jolly",    "Zippy",    "Wiggly",   "Squishy",  "Crunchy",  "Cosmic",   "Mighty",
    "Tiny",     "Jumbo",    "Dizzy",    "Goofy",    "Silly",    "Funky",    "Snazzy",   "Speedy",
    "Fluffy",   "Bubbly",   "Cheesy",   "Noodly",   "Wacky",    "Zany",     "Loopy",    "Rusty",
    "Frosty",   "Sunny",    "Clumsy",   "Brave",    "Clever",   "Happy",    "Lucky",    "Plucky",
    "Peppy",    "Snappy",   "Twirly",   "Crispy",   "Toasty",   "Wonky",    "Bumpy",    "Fancy",
    "Dapper",   "Mega",     "Super",    "Royal",    "Purple",   "Hungry",   "Sneezy",   "Ticklish",
    "Squeaky",  "Rumbly",   "Stinky",   "Burpy",    "Polite",   "Galactic", "Electric", "Atomic",
    "Spooky",   "Glittery", "Flappy",   "Jazzy",    "Bashful",  "Merry",    "Chirpy",   "Zesty",
    "Minty",    "Muddy",    "Puzzled",  "Stripy",   "Spotty",   "Jumpy",    "Hoppy",    "Zoomy",
    "Breezy",   "Cuddly",   "Quirky",   "Nifty",    "Groovy",   "Epic",     "Heroic",   "Daring",
    "Rowdy",    "Shiny",    "Golden",   "Chilly",   "Crafty",   "Rubbery",  "Sugary",   "Salty",
    "Hiccupy",  "Yawning",  "Dancing",  "Singing",  "Juggling", "Skating",  "Flying",   "Gigantic",
    "Mini",     "Bold",
};

const char* const kSecond[] = {
    "Llama",    "Noodle",   "Penguin",  "Waffle",   "Potato",   "Pancake",  "Walrus",   "Narwhal",
    "Platypus", "Wizard",   "Ninja",    "Pirate",   "Robot",    "Dragon",   "Unicorn",  "Yeti",
    "Sloth",    "Hamster",  "Otter",    "Badger",   "Moose",    "Goose",    "Duckling", "Cupcake",
    "Nugget",   "Meatball", "Burrito",  "Pretzel",  "Donut",    "Bagel",    "Turnip",   "Radish",
    "Sprout",   "Gecko",    "Iguana",   "Koala",    "Panda",    "Wombat",   "Lobster",  "Squid",
    "Octopus",  "Gumdrop",  "Biscuit",  "Sock",     "Teapot",   "Kazoo",    "Banjo",    "Tuba",
    "Comet",    "Gnome",    "Puffin",   "Toucan",   "Flamingo", "Hippo",    "Rhino",    "Chicken",
    "Kitten",   "Puppy",    "Bunny",    "Frog",     "Toad",     "Snail",    "Captain",  "Dinosaur",
    "Cactus",   "Pumpkin",  "Avocado",  "Broccoli", "Popcorn",  "Pudding",  "Seahorse", "Beetle",
    "Hedgehog", "Chipmunk", "Ferret",   "Parrot",   "Alpaca",   "Goblin",   "Knight",   "Cowboy",
    "Viking",   "Tornado",  "Volcano",  "Dumpling", "Spud",     "Snowman",  "Sandwich", "Lemon",
    "Mango",    "Kiwi",     "Slipper",  "Bubble",   "Button",   "Pebble",   "Gizmo",    "Widget",
    "Sprocket", "Gadget",   "Meatloaf", "Tater",
};

constexpr int kFirstN = int(sizeof kFirst / sizeof kFirst[0]);
constexpr int kSecondN = int(sizeof kSecond / sizeof kSecond[0]);

} // namespace

int first_count() { return kFirstN; }
int second_count() { return kSecondN; }
const char* first(int i) { return i >= 0 && i < kFirstN ? kFirst[i] : "?"; }
const char* second(int i) { return i >= 0 && i < kSecondN ? kSecond[i] : "?"; }

void format(uint16_t a, uint16_t b, char* out, size_t cap)
{
    snprintf(out, cap, "%s %s", first(a), second(b));
}

void random_pair(uint32_t seed, uint16_t* a, uint16_t* b)
{
    // A quick mix, so neighbouring seeds (board addresses) give unrelated names
    uint32_t x = seed * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    *a = uint16_t((x & 0xFFFF) % kFirstN);
    *b = uint16_t((x >> 16) % kSecondN);
}

bool parse(const char* name, uint16_t* a, uint16_t* b)
{
    if (!name) return false;
    const char* sp = strchr(name, ' ');
    if (!sp) return false;
    for (int i = 0; i < kFirstN; ++i) {
        if (strlen(kFirst[i]) != size_t(sp - name) || strncmp(kFirst[i], name, sp - name) != 0) continue;
        for (int j = 0; j < kSecondN; ++j)
            if (strcmp(kSecond[j], sp + 1) == 0) { *a = uint16_t(i); *b = uint16_t(j); return true; }
    }
    return false;
}

} // namespace names
