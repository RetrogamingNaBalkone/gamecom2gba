#ifndef RE2_ACTOR_H
#define RE2_ACTOR_H
struct Re2Actor {
    unsigned kind, facing, depth, action, frame;
    int x, y;
    unsigned health, age, stagger;
};
int re2_actor_advance(struct Re2Actor *actor);
int re2_actor_delta(const struct Re2Actor *actor);
unsigned re2_actor_random(unsigned limit);
void re2_actor_action(struct Re2Actor *actor,unsigned action);
#endif
