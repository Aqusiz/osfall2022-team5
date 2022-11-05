# Improving WRR Scheduler

## 1. Load Balancing 개선
현재 load balancing은 정해진 시간마다 반드시 수행된다. 그러나 load balancing은 lock을 요구하기 때문에 멀티프로세싱에서 비효율을 일으킨다.
이를 개선하기 위해 정해진 시간마다 반드시 수행하는 것이 아닌 정해진 시간마다 체크만 하고 필요할 때만 수행하도록 할 수 있다.
실제로 weight_sum은 rq에 저장되기 때문에 rq만 체크하여 판단을 할 수 있다.

## 2. CPU 성능 고려
현재 구현방식은 weighted_sum을 최대한 균등하게 배분하는 방식이다. 그러나 만약 cpu마다 성능이 다르다면 굳이 그렇게 할 필요 없다.
(weighted_sum/성능)을 균등하게 갖도록 하면 더 효율적으로 작동하도록 할 수 있을 것이다. 