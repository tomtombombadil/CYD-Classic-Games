#include "names.h"

#include <cstdio>
#include <cstring>

namespace names {

namespace {

// APPEND-ONLY (see names.h): new words go at the end of a list.
const char* const kFirst[] = {
    "Wobbly",   "Turbo",    "Fuzzy",    "Sneaky",   "Bouncy",   "Giggly",   "Sleepy",   "Grumpy",
    "Sparkly",  "Jolly",    "Zippy",    "Wiggly",   "Soggy",    "Squishy",  "Crunchy",  "Cosmic",
    "Mighty",   "Tiny",     "Jumbo",    "Dizzy",    "Goofy",    "Silly",    "Funky",    "Snazzy",
    "Speedy",   "Fluffy",   "Bubbly",   "Cheesy",   "Noodly",   "Wacky",    "Zany",     "Loopy",
    "Rusty",    "Frosty",   "Sunny",    "Stormy",   "Clumsy",   "Brave",    "Clever",   "Happy",
    "Lucky",    "Plucky",   "Peppy",    "Snappy",   "Twirly",   "Swirly",   "Crispy",   "Toasty",
    "Wonky",    "Bumpy",    "Fancy",    "Dapper",   "Mega",     "Super",    "Royal",    "Secret",
    "Purple",   "Hungry",   "Sneezy",   "Ticklish", "Squeaky",  "Rumbly",   "Stinky",   "Burpy",
    "Polite",   "Galactic", "Electric", "Atomic",   "Spooky",   "Glittery", "Flappy",   "Floppy",
    "Jazzy",    "Bashful",  "Merry",    "Chirpy",   "Zesty",    "Minty",    "Muddy",    "Puzzled",
    "Stripy",   "Spotty",   "Jumpy",    "Hoppy",    "Zoomy",    "Breezy",   "Cuddly",   "Quirky",
    "Nifty",    "Groovy",   "Epic",     "Heroic",   "Daring",   "Rowdy",    "Shiny",    "Golden",
    "Chilly",   "Crafty",   "Zigzag",   "Rubbery",  "Sugary",   "Salty",    "Hiccupy",  "Yawning",
    "Dancing",  "Singing",  "Juggling", "Skating",  "Flying",   "Gigantic", "Mini",     "Bold",
};

const char* const kSecond[] = {
    "Pickle",   "Llama",    "Noodle",   "Penguin",  "Taco",     "Waffle",   "Potato",   "Muffin",
    "Pancake",  "Walrus",   "Narwhal",  "Platypus", "Wizard",   "Ninja",    "Pirate",   "Robot",
    "Dragon",   "Unicorn",  "Yeti",     "Sloth",    "Hamster",  "Otter",    "Badger",   "Moose",
    "Goose",    "Duckling", "Banana",   "Cupcake",  "Nugget",   "Meatball", "Burrito",  "Pretzel",
    "Donut",    "Bagel",    "Turnip",   "Radish",   "Sprout",   "Gecko",    "Iguana",   "Koala",
    "Panda",    "Wombat",   "Lobster",  "Squid",    "Octopus",  "Gumdrop",  "Biscuit",  "Sock",
    "Teapot",   "Kazoo",    "Trombone", "Banjo",    "Tuba",     "Rocket",   "Comet",    "Gnome",
    "Monkey",   "Puffin",   "Toucan",   "Flamingo", "Hippo",    "Rhino",    "Chicken",  "Kitten",
    "Puppy",    "Bunny",    "Frog",     "Toad",     "Snail",    "Captain",  "Dinosaur", "Cactus",
    "Pumpkin",  "Coconut",  "Avocado",  "Broccoli", "Carrot",   "Popcorn",  "Pudding",  "Seahorse",
    "Beetle",   "Hedgehog", "Raccoon",  "Chipmunk", "Ferret",   "Parrot",   "Alpaca",   "Goblin",
    "Knight",   "Cowboy",   "Viking",   "Tornado",  "Volcano",  "Dumpling", "Spud",     "Snowman",
    "Sandwich", "Lemon",    "Mango",    "Kiwi",     "Slipper",  "Bubble",   "Button",   "Pebble",
    "Doodle",   "Gizmo",    "Widget",   "Sprocket", "Gadget",   "Zucchini", "Meatloaf", "Tater",
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
